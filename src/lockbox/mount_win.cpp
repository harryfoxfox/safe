/*
  Lockbox: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <lockbox/mount_win.hpp>

#include <lockbox/fs.hpp>
#include <lockbox/util.hpp>
#include <lockbox/webdav_server.hpp>
#include <lockbox/windows_string.hpp>

#include <davfuse/util_sockets.h>
#include <davfuse/webdav_server.h>

#include <sstream>
#include <stdexcept>
#include <string>

#include <ctime>

#include <winhttp.h>

namespace lockbox { namespace win {

static
void
_post_http(const std::string & host, port_t port, const std::string & path) {
  // Use WinHttpOpen to obtain a session handle.
  auto session =
    WinHttpOpen(L"WinHTTP Example/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) throw std::runtime_error("Couldn't create session");
  auto _destroy_session =
    lockbox::create_destroyer(session, WinHttpCloseHandle);

  // Specify an HTTP server.
  auto connect =
    WinHttpConnect(session, w32util::widen(host).c_str(), port, 0);
  if (!connect) throw std::runtime_error("Couldn't create connection");
  auto _destroy_connect =
    lockbox::create_destroyer(connect, WinHttpCloseHandle);

  // Create an HTTP request handle.
  auto request =
    WinHttpOpenRequest(connect, L"POST",
                       w32util::widen(path).c_str(),
                       NULL, WINHTTP_NO_REFERER,
                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                       0);
  if (!request) throw std::runtime_error("Couldn't create requestr");
  auto _destroy_request =
    lockbox::create_destroyer(request, WinHttpCloseHandle);

  // Send a request.
  auto result = WinHttpSendRequest(request,
                                   WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0,
                                   0, 0);
  if (!result) throw std::runtime_error("failure to send request!");
}

bool
MountDetails::is_still_mounted() const {
  auto drive_bitmask = GetLogicalDrives();
  return ((drive_bitmask >> (int) _drive_letter) & 0x1);
}

void
MountDetails::signal_stop() const {
  // check if the thread is already dead
  DWORD exit_code;
  auto success_getexitcode =
    GetExitCodeThread(_thread_handle.get(), &exit_code);
  if (!success_getexitcode) {
    throw std::runtime_error("Couldn't get exit code");
  }

  // thread is done, we out
  if (exit_code != STILL_ACTIVE) return;

  // have to send http signal to stop
  _post_http("localhost", _listen_port, WEBDAV_SERVER_QUIT_URL);
}

void
MountDetails::unmount() {
  std::ostringstream os;
  os << "use " << _drive_letter <<  ": /delete";
  auto ret_shell1 = (int) ShellExecuteW(NULL, L"open",
                                        L"c:\\windows\\system32\\net.exe",
                                        w32util::widen(os.str()).c_str(),
                                        NULL, SW_HIDE);
  if (ret_shell1 <= 32) throw w32util::windows_error();
}

void
MountDetails::open_mount() const {
  auto ret_shell2 =
    (int) ShellExecuteW(NULL, L"open",
                        w32util::widen(std::to_string(_drive_letter) +
                                       ":\\").c_str(),
                        NULL, NULL, SW_SHOWNORMAL);
  if (ret_shell2 <= 32) throw w32util::windows_error();
}

void
MountDetails::disconnect_clients() const {
  _post_http("localhost", _listen_port, WEBDAV_SERVER_DISCONNECT_URL);
}

static
DriveLetter
find_free_drive_letter() {
  // first get all used drive letters
  auto drive_bitmask = GetLogicalDrives();
  if (!drive_bitmask) {
    throw std::runtime_error("Error while calling GetLogicalDrives");
  }

  // then iterate from A->Z until we find one
  for (unsigned i = 0; i < 26; ++i) {
    // is this bit not set => is this drive not in use?
    if (!((drive_bitmask >> i) & 0x1)) {
      return (DriveLetter) i;
    }
  }

  throw std::runtime_error("No drive letter available!");
}

class MountEvent {
  mutable CRITICAL_SECTION _cs;
  lockbox::ManagedResource<HANDLE, CloseHandleDeleter> _event;
  bool _msg_sent;
  bool _error;
  port_t _listen_port;

  void
  _set_mount_status(port_t listen_port, bool error) {
    EnterCriticalSection(&_cs);
    auto _deferred_unlock = lockbox::create_deferred(LeaveCriticalSection, &_cs);

    if (_msg_sent) throw std::runtime_error("Message already sent!");

    auto success_set = SetEvent(_event.get());
    if (!success_set) throw std::runtime_error("failed to set event!");

    _error = error;
    _listen_port = listen_port;
    _msg_sent = true;
  }

public:
  MountEvent()
    : _msg_sent(false)
    , _error(false) {
    auto event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) throw w32util::windows_error();
    _event.reset(event);
    InitializeCriticalSection(&_cs);
  }

  void
  set_mount_success(port_t listen_port) {
    _set_mount_status(listen_port, false);
  }

  void
  set_mount_fail() {
    _set_mount_status(0, true);
  }

  void
  set_thread_done() {
  }

  opt::optional<port_t>
  wait_for_mount_event() const {
    auto res = WaitForSingleObject(_event.get(), INFINITE);
    if (res != WAIT_OBJECT_0) throw w32util::windows_error();

    EnterCriticalSection(&_cs);
    auto _deferred_unlock = lockbox::create_deferred(LeaveCriticalSection, &_cs);
    assert(_msg_sent);
    return (_error
            ? opt::nullopt
            : opt::make_optional(_listen_port));
  }
};

struct ServerThreadParams {
  std::shared_ptr<MountEvent> mount_event_p;
  std::shared_ptr<encfs::FsIO> native_fs;
  encfs::Path encrypted_directory_path;
  encfs::EncfsConfig encfs_config;
  encfs::SecureMem password;
  std::string mount_name;
};

WINAPI
static
DWORD
mount_thread(LPVOID params_) {
  // TODO: catch all exceptions, since this is a top-level

  auto params =
    std::unique_ptr<ServerThreadParams>((ServerThreadParams *) params_);

  std::srand(std::time(nullptr));

  auto enc_fs =
    lockbox::create_enc_fs(std::move(params->native_fs),
                           params->encrypted_directory_path,
                           std::move(params->encfs_config),
                           std::move(params->password));

  // we only listen on localhost
  auto ip_addr = LOCALHOST_IP;
  auto listen_port =
    find_random_free_listen_port(ip_addr, PRIVATE_PORT_START, PRIVATE_PORT_END);

  bool sent_signal = false;

  auto our_callback = [&] (event_loop_handle_t /*loop*/) {
    params->mount_event_p->set_mount_success(listen_port);
    sent_signal = true;
  };

  lockbox::run_webdav_server(std::move(enc_fs),
                             std::move(params->encrypted_directory_path),
                             ip_addr,
                             listen_port,
                             std::move(params->mount_name),
                             our_callback);

  if (!sent_signal) params->mount_event_p->set_mount_fail();

  params->mount_event_p->set_thread_done();

  // server is done, possible unmount
  return 0;
}


MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password) {
  auto mount_name = encrypted_container_path.basename();

  auto mount_event_p = std::make_shared<MountEvent>();
  auto thread_params = new ServerThreadParams {
    mount_event_p,
    native_fs,
    encrypted_container_path,
    cfg,
    password,
    mount_name,
  };

  auto thread_handle =
    create_managed_thread_handle(CreateThread(NULL, 0, mount_thread,
                                              (LPVOID) thread_params, 0, NULL));
  if (!thread_handle) {
    delete thread_params;
    throw std::runtime_error("couldn't create mount thread");
  }

  auto maybe_listen_port = mount_event_p->wait_for_mount_event();
  if (!maybe_listen_port) throw std::runtime_error("mount failed");

  // server is now running, now we can ask the OS to mount it
  auto listen_port = *maybe_listen_port;
  auto drive_letter = find_free_drive_letter();

  std::ostringstream os;
  os << "use " << drive_letter <<
    // we wrap the url in quotes since it could have a space, etc.
    // we don't urlencode it because windows will do that for us
    ": \"http://localhost:" << listen_port << "/" <<
    lockbox::escape_double_quotes(mount_name) << "\"";
  auto ret_shell1 = (int) ShellExecuteW(NULL, L"open",
                                        L"c:\\windows\\system32\\net.exe",
                                        w32util::widen(os.str()).c_str(),
                                        NULL, SW_HIDE);
  if (ret_shell1 <= 32) throw w32util::windows_error();

  auto toret = MountDetails(drive_letter,
                            mount_name,
                            std::move(thread_handle),
                            listen_port,
                            encrypted_container_path);

  // there is a race condition here, the user (or something else) could have
  // unmounted the drive immediately after we mounted it,
  // although it's extremely rare
  // TODO: add a timeout here
  while (!toret.is_still_mounted());

  return std::move(toret);
}

}}

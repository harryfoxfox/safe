/*
  Safe: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <safe/win/mount.hpp>

#include <safe/mount_common.hpp>
#include <safe/util.hpp>
#include <safe/win/util.hpp>
#include <safe/webdav_server.hpp>
#include <safe/win/last_throw_backtrace.hpp>
#include <w32util/string.hpp>
#include <w32util/sync.hpp>

#include <encfs/fs/FileUtils.h>
#include <encfs/fs/FsIO.h>

#include <sstream>
#include <stdexcept>
#include <string>

#include <windows.h>
#include <shellapi.h>

namespace safe { namespace win {

const char NET_BINARY_PATH[] = "c:\\windows\\system32\\net.exe";
const wchar_t NET_BINARY_PATH_W[] = L"c:\\windows\\system32\\net.exe";

bool
MountDetails::is_still_mounted() const {
  auto drive_bitmask = GetLogicalDrives();
  return ((drive_bitmask >> (int) _drive_letter) & 0x1);
}

void
MountDetails::unmount() {
  std::ostringstream os;
  os << "use " << _drive_letter <<  ": /delete";
  auto ret_shell1 =
    (INT_PTR) ShellExecuteW(NULL, L"open",
			    NET_BINARY_PATH_W,
			    w32util::widen(os.str()).c_str(),
			    NULL, SW_HIDE);
  if (ret_shell1 <= 32) w32util::throw_windows_error();
}

void
MountDetails::open_mount() const {
  auto ret_shell2 =
    (INT_PTR) ShellExecuteW(NULL, L"open",
			    w32util::widen(std::to_string(_drive_letter) +
					   ":\\").c_str(),
			    NULL, NULL, SW_SHOWNORMAL);
  if (ret_shell2 <= 32) w32util::throw_windows_error();
}

void
MountDetails::disconnect_clients() const {
  _ws.signal_disconnect_all_clients();
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
  w32util::CriticalSection _cs;
  ManagedHandle _event;
  bool _msg_sent;
  enum {
    EVENT_TYPE_SUCCESS,
    EVENT_TYPE_FAIL,
    EVENT_TYPE_EXCEPTION,
  } _event_type;
  port_t _listen_port;
  opt::optional<WebdavServerHandle> _ws;
  std::exception_ptr _eptr;
  safe::win::Backtrace _bt;

  template <class F>
  void
  _receive_event(F f) {
    auto _deferred_unlock = _cs.create_guard();

    if (_msg_sent) throw std::runtime_error("Message already sent!");
    f();
    _msg_sent = true;

    auto success_set = SetEvent(_event.get());
    if (!success_set) throw std::runtime_error("failed to set event!");
  }

  void
  _set_mount_status(port_t listen_port, opt::optional<WebdavServerHandle> ws, bool error) {
    _receive_event([&] {
        _event_type = error ? EVENT_TYPE_FAIL : EVENT_TYPE_SUCCESS;
        _listen_port = listen_port;
        _ws = std::move(ws);
      });
  }

public:
  MountEvent()
    : _msg_sent(false) {
    auto event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) w32util::throw_windows_error();
    _event.reset(event);
  }

  void
  set_mount_success(port_t listen_port, WebdavServerHandle ws) {
    _set_mount_status(listen_port, std::move(ws), false);
  }

  void
  set_mount_fail() {
    _set_mount_status(0, opt::nullopt, true);
  }

  void
  set_mount_exception(std::exception_ptr eptr, safe::win::Backtrace bt) {
    _receive_event([&] {
        _event_type = EVENT_TYPE_EXCEPTION;
        _eptr = std::move(eptr);
        _bt = std::move(bt);
      });
  }

  void
  set_thread_done() {
  }

  opt::optional<std::pair<port_t, WebdavServerHandle>>
  wait_for_mount_event() {
    auto res = WaitForSingleObject(_event.get(), INFINITE);
    if (res != WAIT_OBJECT_0) w32util::throw_windows_error();

    auto _deferred_unlock = _cs.create_guard();
    assert(_msg_sent);

    switch (_event_type) {
    case EVENT_TYPE_SUCCESS:
      return opt::make_optional(std::make_pair(std::move(_listen_port), std::move(*_ws)));
    case EVENT_TYPE_FAIL:
      return opt::nullopt;
    case EVENT_TYPE_EXCEPTION:
      safe::win::set_last_throw_backtrace(_bt);
      std::rethrow_exception(_eptr);
    default:
      /* notreached */
      assert(false);
      return opt::nullopt;
    }
  }
};

WINAPI
static
DWORD
mount_thread(LPVOID params_) {
  safe::mount_thread_fn_common((ServerThreadParams<MountEvent> *) params_);
  return 0;
}

const port_t HTTP_PORT = 80;

MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password) {
  auto mount_name = encrypted_container_path.basename();

  auto ramdisk_handle = engage_ramdisk();

  auto requested_listen_port = running_on_winxp()
    // windows xp can only mount port 80 :/
    ? opt::make_optional(HTTP_PORT)
    : opt::nullopt;

  auto mount_event_p = std::make_shared<MountEvent>();
  auto thread_params = safe::make_unique<ServerThreadParams<MountEvent>>
    (ServerThreadParams<MountEvent>{
      mount_event_p,
      native_fs,
      encrypted_container_path,
      cfg,
      password,
      mount_name,
      requested_listen_port,
      });

  auto thread_handle =
    create_managed_thread_handle(CreateThread(NULL, 0, mount_thread,
                                              (LPVOID) thread_params.get(), 0, NULL));
  if (!thread_handle) {
    throw std::runtime_error("couldn't create mount thread");
  }

  // thread now owns thread_params
  thread_params.release();

  auto maybe_listen_port_ws_handle = mount_event_p->wait_for_mount_event();
  if (!maybe_listen_port_ws_handle) throw std::runtime_error("mount failed");

  auto & webdav_server_handle = maybe_listen_port_ws_handle->second;

  // server is now running, now we can ask the OS to mount it
  auto listen_port = maybe_listen_port_ws_handle->first;
  auto drive_letter = find_free_drive_letter();

  std::ostringstream parameters_builder;
  parameters_builder << "use " << drive_letter <<
    // we wrap the url in quotes since it could have a space, etc.
    // we don't urlencode it because windows will do that for us
    ": \"" << get_webdav_url_root(listen_port) <<
    safe::escape_double_quotes(mount_name) << "\" " <<
    "/persistent:no";

  auto ret_code = run_command_sync(NET_BINARY_PATH,
                                   parameters_builder.str());
  if (ret_code != EXIT_SUCCESS) throw std::runtime_error("net failed");

  return MountDetails(drive_letter,
                      mount_name,
                      std::move(thread_handle),
                      listen_port,
                      encrypted_container_path,
                      std::move(webdav_server_handle),
                      std::move(ramdisk_handle));
}

}}

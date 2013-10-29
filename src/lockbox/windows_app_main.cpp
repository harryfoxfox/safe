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

#include <lockbox/lockbox_server.hpp>
#include <lockbox/windows_async.hpp>
#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_string.hpp>
#include <lockbox/util.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/fs/FileUtils.h>

#include <encfs/cipher/MemoryPool.h>

#include <encfs/base/optional.h>

#include <davfuse/event_loop.h>
#include <davfuse/log_printer.h>
#include <davfuse/logging.h>
#include <davfuse/webdav_server.h>

#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <sstream>

#include <cstdint>

#include <windows.h>
#include <CommCtrl.h>
#include <Shellapi.h>
#include <Shlobj.h>
#include <dbt.h>
#include <Winhttp.h>

// TODO:
// 1) Unmount drives from tray
// 2) Only one instance at a time
// 3) fix GUI fonts/ok/cancel
// 4) Icons
// 5) opening dialog
// 6) Unmount when webdav server unexpectedly stops (defensive coding)

struct CloseHandleDeleter {
  void operator()(HANDLE a) {
    CloseHandle(a);
  }
};

typedef std::unique_ptr<std::remove_pointer<HANDLE>::type,
                        CloseHandleDeleter> ManagedThreadHandle;

ManagedThreadHandle create_managed_thread_handle(HANDLE a) {
  return ManagedThreadHandle(a);
}

const wchar_t MAIN_WINDOW_CLASS_NAME[] = L"lockbox_tray_icon";
const WORD IDC_STATIC = ~0;
const WORD IDPASSWORD = 200;
const WORD IDCONFIRMPASSWORD = 201;
const auto MAX_PASS_LEN = 256;
const wchar_t TRAY_ICON_TOOLTIP[] = L"Lockbox";

enum class DriveLetter {
  A, B, C, E, F, G, H, I, J, K, L, M, N, O, P,
  Q, R, S, T, U, V, W, X, Y, Z,
};

namespace std {
  std::string to_string(DriveLetter v) {
    return std::string(1, (int) v + 'A');
  }
}

template<class CharT, class Traits>
std::basic_ostream<CharT, Traits> &
operator<<(std::basic_ostream<CharT, Traits> & os, DriveLetter dl) {
  return os << std::to_string(dl);
}

struct MountDetails {
  DriveLetter drive_letter;
  std::string name;
  ManagedThreadHandle thread_handle;
  port_t listen_port;
};

struct WindowData {
  std::shared_ptr<encfs::FsIO> native_fs;
  std::vector<MountDetails> mounts;
  bool is_stopping;
  bool is_opening;
};

struct ServerThreadParams {
  DWORD main_thread;
  std::shared_ptr<encfs::FsIO> native_fs;
  encfs::Path encrypted_directory_path;
  encfs::EncfsConfig encfs_config;
  encfs::SecureMem password;
  std::string mount_name;
};

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

static
void
stop_drive_thread(HANDLE thread_handle, port_t listen_port) {
  // check if the thread is already dead
  DWORD exit_code;
  auto success_getexitcode =
    GetExitCodeThread(thread_handle, &exit_code);
  if (!success_getexitcode) {
    throw std::runtime_error("Couldn't get exit code");
  }

  // thread is done, we out
  if (exit_code != STILL_ACTIVE) return;

  // have to send http signal to stop
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
    WinHttpConnect(session, L"localhost", listen_port, 0);
  if (!connect) throw std::runtime_error("Couldn't create connection");
  auto _destroy_connect =
    lockbox::create_destroyer(connect, WinHttpCloseHandle);

  // Create an HTTP request handle.
  auto request =
    WinHttpOpenRequest(connect, L"POST",
                       w32util::widen(WEBDAV_SERVER_QUIT_URL).c_str(),
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

static
void
stop_relevant_drive_threads(std::vector<MountDetails> & mounts) {
  DWORD last_bitmask = 0;

  while (true) {
    auto drive_bitmask = GetLogicalDrives();
    if (!drive_bitmask) {
      throw std::runtime_error("Error while calling GetLogicalDrives");
    }

    if (drive_bitmask == last_bitmask) return;

    for (auto it = mounts.begin(); it != mounts.end();) {
      const auto & md = *it;
      if (!((drive_bitmask >> (int) md.drive_letter) & 0x1)) {
        log_debug("Drive %s: is no longer mounted, stopping thread",
                  std::to_string(md.drive_letter).c_str());
        auto success =
          w32util::run_async_void(stop_drive_thread,
                                  md.thread_handle.get(),
                                  md.listen_port);
        // if we got a quit message
        if (!success) return;

        // now wait for thread to die
        // TODO: this is not totally necessary
        //       to defensively code, we might want to instead wait on
        //       a finite timeout and quit or throw an exception if it
        //       runs out
        while (true) {
          auto ptr = md.thread_handle.get();
          auto ret =
            MsgWaitForMultipleObjects(1, &ptr,
                                      FALSE, INFINITE, QS_ALLINPUT);
          if (ret == WAIT_OBJECT_0) break;
          else if (ret == WAIT_OBJECT_0 + 1) {
            // we have a message - peek and dispatch it
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
              if (msg.message == WM_QUIT) {
                PostQuitMessage(msg.wParam);
                return;
              }
              TranslateMessage(&msg);
              DispatchMessage(&msg);
            }
          }
          // random error
          else return;
        }

        // thread has died, now delete this mount
        it = mounts.erase(it);
      }
      else ++it;
    }

    last_bitmask = drive_bitmask;
  }
}

void
quick_alert(HWND owner,
            const std::string &msg,
            const std::string &title) {
  MessageBoxW(owner,
              w32util::widen(msg).c_str(),
              w32util::widen(title).c_str(),
              MB_ICONEXCLAMATION | MB_OK);
}

WINAPI
opt::optional<std::string>
get_folder_dialog(HWND owner) {
  while (true) {
    wchar_t chosen_name[MAX_PATH];
    BROWSEINFOW bi;
    lockbox::zero_object(bi);
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select Encrypted Folder";
    bi.ulFlags = BIF_USENEWUI;
    bi.pszDisplayName = chosen_name;
    auto pidllist = SHBrowseForFolderW(&bi);
    if (pidllist) {
      wchar_t file_buffer_ret[MAX_PATH];
      const auto success = SHGetPathFromIDList(pidllist, file_buffer_ret);
      CoTaskMemFree(pidllist);
      if (success) return w32util::narrow(file_buffer_ret);
      else {
        std::ostringstream os;
        os << "Your selection \"" << w32util::narrow(bi.pszDisplayName) <<
          "\" is not a valid folder!";
        quick_alert(owner, os.str(), "Bad Selection!");
      }
    }
    else return opt::nullopt;
  }
}

void
clear_field(HWND hwnd, WORD id, size_t num_chars){
  auto zeroed_bytes =
    std::unique_ptr<wchar_t[]>(new wchar_t[num_chars + 1]);
  memset(zeroed_bytes.get(), 0xaa, num_chars * sizeof(wchar_t));
  zeroed_bytes[num_chars] = 0;
  SetDlgItemTextW(hwnd, id, zeroed_bytes.get());
}


encfs::SecureMem
securely_read_password_field(HWND hwnd, WORD id, bool clear = true) {
  // securely get what's in dialog box
  auto st1 = encfs::SecureMem((MAX_PASS_LEN + 1) * sizeof(wchar_t));
  auto num_chars =
    GetDlgItemTextW(hwnd, id,
                    (wchar_t *) st1.data(),
                    st1.size() / sizeof(wchar_t));

  // attempt to clear what's in dialog box
  if (clear) clear_field(hwnd, id, num_chars);

  // convert wchars to utf8
  auto st2 = encfs::SecureMem(MAX_PASS_LEN * 3);
  size_t ret;
  if (num_chars) {
    ret = w32util::narrow_into_buf((wchar_t *) st1.data(), num_chars,
                                   (char *) st2.data(), st2.size() - 1);
    // should never happen
    if (!ret) throw std::runtime_error("fail");
  }
  else ret = 0;

  st2.data()[ret] = '\0';
  return std::move(st2);
}

bool
open_mount(HWND owner, const MountDetails & md) {
  auto ret_shell2 =
    (int) ShellExecuteW(owner, L"open",
                        w32util::widen(std::to_string(md.drive_letter) +
                                       ":\\").c_str(),
                        NULL, NULL, SW_SHOWNORMAL);
  return ret_shell2 > 32;
}

CALLBACK
BOOL
get_password_dialog_proc(HWND hwnd, UINT Message,
                         WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG:
    SendDlgItemMessage(hwnd, IDPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    return TRUE;
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK: {
      auto secure_pass = securely_read_password_field(hwnd, IDPASSWORD);
      auto st2 = new encfs::SecureMem(std::move(secure_pass));
      EndDialog(hwnd, (INT_PTR) st2);
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) 0);
      break;
    }
    }
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

WINAPI
static
opt::optional<encfs::SecureMem>
get_password_dialog(HWND hwnd, const encfs::Path & /*path*/) {
  using namespace w32util;

  auto dlg =
    DialogTemplate(DialogDesc(DS_MODALFRAME | WS_POPUP |
                              WS_SYSMENU | WS_VISIBLE |
                              WS_CAPTION,
                              "Enter Your Password",
                              0, 0, 100, 66),
                   {
                     CText("Enter Your Password", IDC_STATIC,
                           15, 10, 70, 33),
                     EditText(IDPASSWORD, 10, 20, 80, 12,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     DefPushButton("&OK", IDOK,
                                   25, 40, 50, 14),
                   }
                   );

  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      hwnd, get_password_dialog_proc);
  if (!ret_ptr) return opt::nullopt;

  auto ret = (encfs::SecureMem *) ret_ptr;
  auto toret = std::move(*ret);
  delete ret;
  return toret;
}

CALLBACK
static
BOOL
confirm_new_encrypted_container_proc(HWND hwnd, UINT Message,
                                     WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG:
    return TRUE;
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK: {
      EndDialog(hwnd, (INT_PTR) IDOK);
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) IDCANCEL);
      break;
    }
    }
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

WINAPI
static
bool
confirm_new_encrypted_container(HWND owner,
                                const encfs::Path & encrypted_directory_path) {
  using namespace w32util;

  std::ostringstream os;
  os << "The folder you selected:\r\n\r\n\"" <<
    (const std::string &) encrypted_directory_path <<
    ("\"\r\n\r\ndoes not appear to be an encrypted container.\r\n"
     "Would you like to create one there?");
  auto dialog_text = os.str();

  // TODO: compute this programmatically
  const auto FONT_HEIGHT = 8;
  // 5.5 is the width of the 'm' char  with the default font
  // since it's the widest character we just multiply it by 3/4
  // to get the 'average' width (crude i know)
  const auto FONT_WIDTH = 5.5 / 2;

  typedef unsigned unit_t;
  const unit_t DIALOG_WIDTH =
    std::max((unit_t) 175,
             (unit_t)
             (20 +
              FONT_WIDTH *
              (((const std::string &) encrypted_directory_path).size() + 2)));
  const unit_t TOP_MARGIN = FONT_HEIGHT;
  const unit_t MIDDLE_MARGIN = FONT_HEIGHT;
  const unit_t BOTTOM_MARGIN = FONT_HEIGHT;
  const unit_t TEXT_WIDTH = 160;
  const unit_t TEXT_HEIGHT = FONT_HEIGHT * 6;
  const unit_t BUTTON_WIDTH = 50;
  const unit_t BUTTON_HEIGHT = 14;
  const unit_t BUTTON_SPACING = 12;

  const auto dlg =
    DialogTemplate(DialogDesc(DS_MODALFRAME | WS_POPUP |
                              WS_SYSMENU | WS_VISIBLE |
                              WS_CAPTION,
                              "No encrypted container found!",
                              0, 0, DIALOG_WIDTH,
                              TOP_MARGIN + MIDDLE_MARGIN + BOTTOM_MARGIN +
                              TEXT_HEIGHT + BUTTON_HEIGHT),
                   {
                     CText(std::move(dialog_text), IDC_STATIC,
                           center_offset(DIALOG_WIDTH, TEXT_WIDTH),
                           TOP_MARGIN,
                           TEXT_WIDTH, TEXT_HEIGHT),
                     PushButton("&Cancel", IDCANCEL,
                                center_offset(DIALOG_WIDTH,
                                            2 * BUTTON_WIDTH + BUTTON_SPACING),
                                TOP_MARGIN + TEXT_HEIGHT + MIDDLE_MARGIN,
                                BUTTON_WIDTH, BUTTON_HEIGHT),
                     DefPushButton("&OK", IDOK,
                                   center_offset(DIALOG_WIDTH,
                                               2 * BUTTON_WIDTH +
                                               BUTTON_SPACING) +
                                   BUTTON_WIDTH + BUTTON_SPACING,
                                   TOP_MARGIN + TEXT_HEIGHT + MIDDLE_MARGIN,
                                   BUTTON_WIDTH, BUTTON_HEIGHT),
                   }
                   );

  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      owner, confirm_new_encrypted_container_proc);
  return ((int) ret_ptr == IDOK);
}


CALLBACK
static
BOOL
get_new_password_dialog_proc(HWND hwnd, UINT Message,
                             WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG: {
    SendDlgItemMessage(hwnd, IDPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    SendDlgItemMessage(hwnd, IDCONFIRMPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    return TRUE;
  }
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDOK: {
      auto secure_pass_1 =
        securely_read_password_field(hwnd, IDPASSWORD, false);
      auto secure_pass_2 =
        securely_read_password_field(hwnd, IDCONFIRMPASSWORD, false);

      auto num_chars_1 = strlen((char *) secure_pass_1.data());
      auto num_chars_2 = strlen((char *) secure_pass_2.data());
      if (num_chars_1 != num_chars_2 ||
          memcmp(secure_pass_1.data(), secure_pass_2.data(), num_chars_1)) {
        quick_alert(hwnd, "The Passwords do not match!",
                    "Passwords don't match");
        return TRUE;
      }

      if (!num_chars_1) {
        quick_alert(hwnd, "Empty password is not allowed!",
                    "Invalid Password");
        return TRUE;
      }

      clear_field(hwnd, IDPASSWORD, num_chars_1);
      clear_field(hwnd, IDCONFIRMPASSWORD, num_chars_2);

      auto st2 = new encfs::SecureMem(std::move(secure_pass_1));
      EndDialog(hwnd, (INT_PTR) st2);
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) 0);
      break;
    }
    }
    break;
  }
  default:
    return FALSE;
  }
  return TRUE;
}

WINAPI
static
opt::optional<encfs::SecureMem>
get_new_password_dialog(HWND owner,
                        const encfs::Path & /*encrypted_directory_path*/) {
  using namespace w32util;

  // TODO: compute this programmatically
  const float FONT_HEIGHT = 8;
  // 5.5 is the width of the 'm' char  with the default font
  // since it's the widest character we just divide it by 2
  // to get the 'average' width (crude i know)
  const float FONT_WIDTH = 5.5 * 0.7;

  typedef unsigned unit_t;
  const unit_t TOP_MARGIN = FONT_HEIGHT;
  const unit_t MIDDLE_MARGIN = FONT_HEIGHT;
  const unit_t BOTTOM_MARGIN = FONT_HEIGHT;

  const unit_t TEXT_WIDTH = 160;
  const unit_t TEXT_HEIGHT = FONT_HEIGHT;

  const unit_t PASS_LABEL_CHARS = strlen("Confirm Password:");

  const unit_t PASS_LABEL_WIDTH = FONT_WIDTH * PASS_LABEL_CHARS;
  const unit_t PASS_LABEL_HEIGHT = FONT_HEIGHT;

  const unit_t PASS_LABEL_SPACE = 0;
  const unit_t PASS_LABEL_VOFFSET = 2;
  const unit_t PASS_MARGIN = FONT_HEIGHT / 2;

  const unit_t PASS_ENTRY_WIDTH = FONT_WIDTH * 16;
  const unit_t PASS_ENTRY_HEIGHT = 12;

  const unit_t BUTTON_WIDTH = 50;
  const unit_t BUTTON_HEIGHT = 14;
  const unit_t BUTTON_SPACING = 12;

  const unit_t DIALOG_WIDTH =
    std::max((unit_t) 175,
             PASS_LABEL_WIDTH + PASS_LABEL_SPACE + PASS_ENTRY_WIDTH + 20);

  const auto dlg =
    DialogTemplate(DialogDesc(DS_MODALFRAME | WS_POPUP |
                              WS_SYSMENU | WS_VISIBLE |
                              WS_CAPTION,
                              "Create Encrypted Container",
                              0, 0, DIALOG_WIDTH,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN +
                              PASS_ENTRY_HEIGHT + PASS_MARGIN +
                              PASS_ENTRY_HEIGHT + MIDDLE_MARGIN +
                              BUTTON_HEIGHT + BOTTOM_MARGIN),
                   {
                     CText("Create Encrypted Container", IDC_STATIC,
                           center_offset(DIALOG_WIDTH, TEXT_WIDTH),
                           TOP_MARGIN,
                           TEXT_WIDTH, TEXT_HEIGHT),
                     LText("New Password:", IDC_STATIC,
                           center_offset(DIALOG_WIDTH,
                                       PASS_LABEL_WIDTH + PASS_LABEL_SPACE +
                                       PASS_ENTRY_WIDTH),
                           TOP_MARGIN +
                           TEXT_HEIGHT + MIDDLE_MARGIN + PASS_LABEL_VOFFSET,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDPASSWORD,
                              center_offset(DIALOG_WIDTH,
                                          PASS_LABEL_WIDTH + PASS_LABEL_SPACE +
                                          PASS_ENTRY_WIDTH) +
                              PASS_LABEL_WIDTH + PASS_LABEL_SPACE,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     LText("Confirm Password:", IDC_STATIC,
                           center_offset(DIALOG_WIDTH,
                                       PASS_LABEL_WIDTH + PASS_LABEL_SPACE +
                                       PASS_ENTRY_WIDTH),
                           TOP_MARGIN +
                           TEXT_HEIGHT + MIDDLE_MARGIN +
                           PASS_ENTRY_HEIGHT + PASS_MARGIN + PASS_LABEL_VOFFSET,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDCONFIRMPASSWORD,
                              center_offset(DIALOG_WIDTH,
                                          PASS_LABEL_WIDTH + PASS_LABEL_SPACE +
                                          PASS_ENTRY_WIDTH) +
                              PASS_LABEL_WIDTH + PASS_LABEL_SPACE,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN +
                              PASS_ENTRY_HEIGHT + PASS_MARGIN,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     PushButton("&Cancel", IDCANCEL,
                                center_offset(DIALOG_WIDTH,
                                            2 * BUTTON_WIDTH + BUTTON_SPACING),
                                TOP_MARGIN +
                                TEXT_HEIGHT + MIDDLE_MARGIN +
                                PASS_ENTRY_HEIGHT + PASS_MARGIN +
                                PASS_ENTRY_HEIGHT + MIDDLE_MARGIN,
                                BUTTON_WIDTH, BUTTON_HEIGHT),
                     DefPushButton("&OK", IDOK,
                                   center_offset(DIALOG_WIDTH,
                                               2 * BUTTON_WIDTH +
                                               BUTTON_SPACING) +
                                   BUTTON_WIDTH + BUTTON_SPACING,
                                   TOP_MARGIN +
                                   TEXT_HEIGHT + MIDDLE_MARGIN +
                                   PASS_ENTRY_HEIGHT + PASS_MARGIN +
                                   PASS_ENTRY_HEIGHT + MIDDLE_MARGIN,
                                   BUTTON_WIDTH, BUTTON_HEIGHT),
                   }
                   );

  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      owner, get_new_password_dialog_proc);
  if (!ret_ptr) return opt::nullopt;

  auto ret = (encfs::SecureMem *) ret_ptr;
  auto toret = std::move(*ret);
  delete ret;
  return toret;
}

const UINT MOUNT_DONE_SIGNAL = WM_APP;
const UINT MOUNT_OVER_SIGNAL = WM_APP + 1;

WINAPI
static
DWORD
mount_thread(LPVOID params_) {
  // TODO: catch all exceptions, since this is a top-level

  auto params =
    std::unique_ptr<ServerThreadParams>((ServerThreadParams *) params_);

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
    PostThreadMessage(params->main_thread, MOUNT_DONE_SIGNAL, 1, listen_port);
    sent_signal = true;
  };

  lockbox::run_lockbox_webdav_server(std::move(enc_fs),
                                     std::move(params->encrypted_directory_path),
                                     ip_addr,
                                     listen_port,
                                     std::move(params->mount_name),
                                     our_callback);

  auto sig = (sent_signal)
    ? MOUNT_OVER_SIGNAL
    : MOUNT_DONE_SIGNAL;

  PostThreadMessage(params->main_thread, sig, 0, 0);

  // server is done, possible unmount
  return 0;
}

WINAPI
opt::optional<MountDetails>
mount_encrypted_folder_dialog(HWND owner,
                              std::shared_ptr<encfs::FsIO> native_fs) {
  auto maybe_chosen_folder = get_folder_dialog(owner);
  // they pressed cancel
  if (!maybe_chosen_folder) return opt::nullopt;
  auto chosen_folder = std::move(*maybe_chosen_folder);

  opt::optional<encfs::Path> maybe_encrypted_directory_path;
  try {
    maybe_encrypted_directory_path =
      native_fs->pathFromString(chosen_folder);
  }
  catch (const std::exception & err) {
    std::ostringstream os;
    os << "Bad path for encrypted directory: " <<
      chosen_folder;
    quick_alert(owner, os.str(), "Bad Path!");
    return opt::nullopt;
  }

  auto encrypted_directory_path =
    std::move(*maybe_encrypted_directory_path);

  // attempt to read configuration
  opt::optional<encfs::EncfsConfig> maybe_encfs_config;
  try {
    maybe_encfs_config =
      w32util::modal_call(owner, "Reading configuration...",
                          "Reading configuration...",
                          encfs::read_config,
                          native_fs, encrypted_directory_path);
    // it was canceled
    if (!maybe_encfs_config) return opt::nullopt;
  }
  catch (const encfs::ConfigurationFileDoesNotExist &) {
    // this is fine, we'll just attempt to create it
  }
  catch (const encfs::ConfigurationFileIsCorrupted &) {
    // this is not okay right now, let the user know and exit
    std::ostringstream os;
    os << "The configuration file in folder \"" <<
      chosen_folder << "\" is corrupted";
    quick_alert(owner, os.str(), "Bad Folder");
    return opt::nullopt;
  }

  opt::optional<encfs::SecureMem> maybe_password;
  if (maybe_encfs_config) {
    // ask for password
    while (!maybe_password) {
      maybe_password =
        get_password_dialog(owner, encrypted_directory_path);
      if (!maybe_password) return opt::nullopt;

      log_debug("verifying password...");
      const auto correct_password =
        w32util::modal_call(owner, "Verifying Password...",
                            "Verifying password...",
                            encfs::verify_password,
                            *maybe_encfs_config, *maybe_password);
      log_debug("verifying done!");
      if (!correct_password) return opt::nullopt;

      if (!*correct_password) {
        quick_alert(owner, "Incorrect password! Try again",
                    "Incorrect Password");
        maybe_password = opt::nullopt;
      }
    }
  }
  else {
    // check if the user wants to create an encrypted container
    auto create =
      confirm_new_encrypted_container(owner, encrypted_directory_path);
    if (!create) return opt::nullopt;

    // create new password
    maybe_password =
      get_new_password_dialog(owner, encrypted_directory_path);
    if (!maybe_password) return opt::nullopt;

    maybe_encfs_config =
      w32util::modal_call(owner,
                          "Creating New Configuration...",
                          "Creating new configuration...",
                          encfs::create_paranoid_config,
                          *maybe_password);
    if (!maybe_encfs_config) return opt::nullopt;

    auto modal_completed =
      w32util::modal_call_void(owner,
                               "Saving New Configuration...",
                               "Saving new configuration...",
                               encfs::write_config,
                               native_fs, encrypted_directory_path,
                               *maybe_encfs_config);
    if (!modal_completed) return opt::nullopt;
  }

  // okay now we have:
  // * a valid path
  // * a valid config
  // * a valid password
  // we're ready to mount the drive

  auto drive_letter = find_free_drive_letter();
  auto mount_name = encrypted_directory_path.basename();

  auto thread_params = new ServerThreadParams {
    GetCurrentThreadId(),
    native_fs,
    std::move(encrypted_directory_path),
    std::move(*maybe_encfs_config),
    std::move(*maybe_password),
    mount_name,
  };

  auto thread_handle =
    create_managed_thread_handle(CreateThread(NULL, 0, mount_thread,
                                              (LPVOID) thread_params, 0, NULL));
  if (!thread_handle) {
    delete thread_params;
    quick_alert(owner,
                "Unable to start encrypted file system!",
                "Error");
    return opt::nullopt;
  }

  auto msg_ptr = w32util::modal_until_message(owner,
                                              "Starting Encrypted Container...",
                                              "Starting encrypted container...",
                                              MOUNT_DONE_SIGNAL);
  // TODO: kill thread
  if (!msg_ptr) return opt::nullopt;

  assert(msg_ptr->message == MOUNT_DONE_SIGNAL);

  if (!msg_ptr->wParam) {
    quick_alert(owner,
                "Unable to start encrypted file system!",
                "Error");
    return opt::nullopt;
  }

  auto listen_port = (port_t) msg_ptr->lParam;

  {
    auto dialog_wnd =
      w32util::create_waiting_modal(owner,
                                    "Mounting Encrypted Container...",
                                    "Mounting encrypted container...");
    // TODO: kill thread
    if (!dialog_wnd) return opt::nullopt;
    auto _destroy_dialog_wnd =
      lockbox::create_destroyer(dialog_wnd, DestroyWindow);

    // disable window
    EnableWindow(owner, FALSE);

    // just mount the file system using the console command
    std::ostringstream os;
    os << "use " << drive_letter <<
      ": http://localhost:" << listen_port << "/" << mount_name;
    auto ret_shell1 = (int) ShellExecuteW(owner, L"open",
                                          L"c:\\windows\\system32\\net.exe",
                                          w32util::widen(os.str()).c_str(),
                                          NULL, SW_HIDE);

    // disable window
    EnableWindow(owner, TRUE);

    // abort if it fails
    if (ret_shell1 <= 32) {
      // TODO kill thread
      log_error("ShellExecuteW error: Ret was: %d", ret_shell1);
      quick_alert(owner,
                  "Unable to start encrypted file system!",
                  "Error");
      return opt::nullopt;
    }
  }

  auto md = MountDetails {
    drive_letter,
    mount_name,
    std::move(thread_handle),
    listen_port,
  };

  // now open up the file system with explorer
  // NB: we try this more than once because it may not be
  //     fully mounted after the previous command
  unsigned i;
  for (i = 0; i < 5; ++i) {
    // this is not that bad, we were just opening the windows
    if (!open_mount(owner, md)) {
      log_error("opening mount failed, trying again...: %s",
                w32util::last_error_message().c_str());
      Sleep(0);
    }
    else break;
  }

  if (i == 5) {
    quick_alert(owner, "Success", "Encrypted file system has been mounted!");
  }

  return std::move(md);
}

static
void
append_string_menu_item(HMENU menu_handle, bool is_default,
                        std::string text, UINT id) {
  auto menu_item_text =
    w32util::widen(std::move(text));

  MENUITEMINFOW mif;
  lockbox::zero_object(mif);

  mif.cbSize = sizeof(mif);
  mif.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_STRING;
  mif.fState = is_default ? MFS_DEFAULT : 0;
  mif.wID = id;
  mif.dwTypeData = const_cast<LPWSTR>(menu_item_text.data());
  mif.cch = menu_item_text.size();

  auto items_added = GetMenuItemCount(menu_handle);
  if (items_added == -1) throw std::runtime_error("GetMenuItemCount");

  auto success_menu_item =
    InsertMenuItemW(menu_handle, items_added, TRUE, &mif);
  if (!success_menu_item) {
    throw std::runtime_error("InsertMenuItem elt");
  }
}

WINAPI
void
open_create_mount_dialog(WindowData & wd, HWND hwnd) {
  if (wd.is_opening) return;

  wd.is_opening = true;
  auto md = mount_encrypted_folder_dialog(hwnd, wd.native_fs);
  if (md) wd.mounts.push_back(std::move(*md));
  wd.is_opening = false;
}

CALLBACK
LRESULT
main_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const static UINT TRAY_ICON_MSG = WM_USER;
  const auto wd = (WindowData *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
  switch(msg){
  case WM_DEVICECHANGE: {
    // NB: the device is actually unmounted in the OS after this message
    //     is finished processing,
    // TODO: we might want to instead execute the following code in a
    ///      asynchronous timer to not block the OS
    if (wParam == DBT_DEVICEREMOVECOMPLETE && !wd->is_stopping) {
      wd->is_stopping = true;
      // if this throws an exception, our app is going to close
      // TODO: might have to wait a bit before running this
      stop_relevant_drive_threads(wd->mounts);
      wd->is_stopping = false;
    }
    return TRUE;
    break;
  }

  case WM_CREATE: {
    // set application state (WindowData) in main window
    const auto wd = (WindowData *) ((LPCREATESTRUCT) lParam)->lpCreateParams;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) wd);

    // add tray icon
    NOTIFYICONDATA icon_data;
    lockbox::zero_object(icon_data);
    icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
    icon_data.hWnd = hwnd;
    icon_data.uID = 0;
    icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon_data.uCallbackMessage = TRAY_ICON_MSG;
    icon_data.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    memcpy(icon_data.szTip, TRAY_ICON_TOOLTIP, sizeof(TRAY_ICON_TOOLTIP));
    icon_data.uVersion = NOTIFYICON_VERSION;

    auto success = Shell_NotifyIcon(NIM_ADD, &icon_data);
    if (!success) {
      // TODO deal with this?
      log_error("Error while adding icon: %s",
                w32util::last_error_message().c_str());
    }
    else {
      // now set tray icon version
      auto success_version = Shell_NotifyIcon(NIM_SETVERSION, &icon_data);
      if (!success_version) {
        // TODO: deal with this
        log_error("Error while setting icon version: %s",
                  w32util::last_error_message().c_str());
      }
    }

    return 0;
    break;
  }

  case TRAY_ICON_MSG: {
    switch (lParam) {
    case WM_LBUTTONDBLCLK: {
      // on left click
      // if there is an active mount, open the folder
      // otherwise allow the user to make a new mount
      if (wd->mounts.empty()) open_create_mount_dialog(*wd, hwnd);
      else {
        auto success = open_mount(hwnd, wd->mounts.front());
        if (!success) {
          log_error("Error while opening mount... %s: %s",
                    std::to_string(wd->mounts.front().drive_letter).c_str(),
                    w32util::last_error_message().c_str());
        }
      }
      break;
    }

    case WM_RBUTTONDOWN: {
      // get x & y offset
      try {
        POINT xypos;
        auto success_pos = GetCursorPos(&xypos);
        if (!success_pos) throw std::runtime_error("GetCursorPos");

        auto menu_handle = CreatePopupMenu();
        if (!menu_handle) throw std::runtime_error("CreatePopupMenu");
        auto _destroy_menu =
          lockbox::create_destroyer(menu_handle, DestroyMenu);

        // okay our menu is like this
        // [ Lockbox A ]
        // [ Lockbox B ]
        // ...
        // -------
        // Open or create a new Lockbox

        // NB: we start at 1 because TrackPopupMenu()
        //     returns 0 if the menu was canceledn
        UINT idx = 1;
        for (const auto & md : wd->mounts) {
          append_string_menu_item(menu_handle,
                                  idx == 1,
                                  ("Open \"" + md.name + "\" (" +
                                   std::to_string(md.drive_letter) + ":)"),
                                  idx);
          ++idx;
        }

        // add separator
        if (!wd->mounts.empty()) {
          MENUITEMINFOW mif;
          lockbox::zero_object(mif);

          mif.cbSize = sizeof(mif);
          mif.fMask = MIIM_FTYPE;
          mif.fType = MFT_SEPARATOR;

          auto items_added = GetMenuItemCount(menu_handle);
          if (items_added == -1) throw std::runtime_error("GetMenuItemCount");
          auto success_menu_item =
            InsertMenuItemW(menu_handle, items_added, TRUE, &mif);
          if (!success_menu_item) {
            throw std::runtime_error("InsertMenuItem sep");
          }
        }

        enum {
          MENU_CREATE_ID = (UINT) -1,
          MENU_QUIT_ID = (UINT) -2,
          MENU_DEBUG_ID = (UINT) -3,
        };

        // add create action
        append_string_menu_item(menu_handle,
                                wd->mounts.empty(),
                                "Open or create a new Lockbox",
                                MENU_CREATE_ID);

        // add quit action
        append_string_menu_item(menu_handle,
                                false,
                                "Quit",
                                MENU_QUIT_ID);

#ifndef NDEBUG
        // add breakpoint action
        append_string_menu_item(menu_handle,
                                false,
                                "DebugBreak",
                                MENU_DEBUG_ID);
#endif

        // NB: have to do this so the menu disappears
        // when you click out of it
        SetForegroundWindow(hwnd);

        SetLastError(0);
        auto selected =
          (UINT) TrackPopupMenu(menu_handle,
                                TPM_LEFTALIGN | TPM_LEFTBUTTON |
                                TPM_BOTTOMALIGN | TPM_RETURNCMD,
                                xypos.x, xypos.y,
                                0, hwnd, NULL);
        if (!selected && GetLastError()) {
          throw std::runtime_error("TrackPopupMenu");
        }

        switch (selected) {
        case 0:
          // menu was canceled
          break;
        case MENU_QUIT_ID: {
          DestroyWindow(hwnd);
          break;
        }
        case MENU_CREATE_ID: {
          open_create_mount_dialog(*wd, hwnd);
          break;
        }
        case MENU_DEBUG_ID: {
          DebugBreak();
          break;
        }
        default: {
          assert(!wd->mounts.empty());
          auto success = open_mount(hwnd, wd->mounts[selected - 1]);
          if (!success) throw std::runtime_error("open_mount");
          break;
        }
        }
      }
      catch (const std::exception & err) {
        log_error("Error while doing: \"%s\": %s",
                  err.what(),
                  w32util::last_error_message().c_str());
      }

      break;
    }

    default: break;
    }

    return 0;
    break;
  }

  case WM_CLOSE: {
    DestroyWindow(hwnd);
    return 0;
    break;
  }

  case WM_DESTROY: {
    PostQuitMessage(0);
    return 0;
    break;
  }

  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  /* not reached, there is no default return code */
  assert(false);
}

WINAPI
int
WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
        LPSTR /*lpCmdLine*/, int nCmdShow) {
  // TODO: catch all exceptions, since this is a top-level

  // TODO: de-initialize
  log_printer_default_init();
  logging_set_global_level(LOG_DEBUG);
  log_debug("Hello world!");

  // TODO: de-initialize
  auto ret_ole = OleInitialize(NULL);
  if (ret_ole != S_OK) throw std::runtime_error("couldn't initialize ole!");

  INITCOMMONCONTROLSEX icex;
  lockbox::zero_object(icex);
  icex.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
  icex.dwSize = sizeof(icex);

  auto success = InitCommonControlsEx(&icex);
  if (success == FALSE) {
    throw std::runtime_error("Couldn't initialize common controls");
  }

  // TODO: require ComCtl32.dll version >= 6.0
  // (we do this in the manifest but it would be nice
  //  to check at runtime too)

  // TODO: de-initialize
  lockbox::global_webdav_init();

  auto native_fs = lockbox::create_native_fs();
  auto wd = WindowData {
    /*.native_fs = */std::move(native_fs),
    /*.mounts = */std::vector<MountDetails>(),
    /*.is_stopping = */false,
    /*.is_opening = */false,
  };

  // register our main window class
  WNDCLASSEXW wc;
  lockbox::zero_object(wc);
  wc.style         = CS_DBLCLKS;
  wc.cbSize        = sizeof(WNDCLASSEX);
  wc.lpfnWndProc   = main_wnd_proc;
  wc.hInstance     = hInstance;
  wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
  wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) COLOR_WINDOW;
  wc.lpszClassName = MAIN_WINDOW_CLASS_NAME;
  wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);

  if (!RegisterClassExW(&wc)) {
    throw std::runtime_error("Failure registering main window class");
  }

  // create our main window
  auto hwnd = CreateWindowExW(WS_EX_CLIENTEDGE,
                              MAIN_WINDOW_CLASS_NAME,
                              L"Lockbox Main Window",
                              WS_OVERLAPPEDWINDOW | WS_SYSMENU | WS_CAPTION,
                              CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                              NULL, NULL, hInstance, &wd);
  if (!hwnd) {
    throw std::runtime_error("Failure to create main window");
  }

  // update window
  UpdateWindow(hwnd);

  (void) nCmdShow;

  // run message loop
  MSG msg;
  BOOL ret_getmsg;
  while ((ret_getmsg = GetMessageW(&msg, NULL, 0, 0))) {
    if (ret_getmsg == -1) break;
    if (msg.message == MOUNT_OVER_SIGNAL) {
      // TODO: webdav server died, kill mount
      // this doesn't work because we could be in an inner msg loop
      log_debug("thread died!");
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // kill all mounts
  for (const auto & mount : wd.mounts) {
    std::ostringstream os;
    os << "use " << mount.drive_letter <<  ": /delete";
    auto ret_shell1 = (int) ShellExecuteW(hwnd, L"open",
                                          L"c:\\windows\\system32\\net.exe",
                                          w32util::widen(os.str()).c_str(),
                                          NULL, SW_HIDE);
    if (ret_shell1 <= 32) {
      log_error("Failed to umount %s:",
                std::to_string(mount.drive_letter).c_str());
    }
  }

  if (ret_getmsg == -1) throw std::runtime_error("getmessage failed!");

  return msg.wParam;
}

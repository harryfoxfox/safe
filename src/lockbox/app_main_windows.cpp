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

#include <lockbox/product_name.h>
#include <lockbox/lockbox_server.hpp>
#include <lockbox/lockbox_strings.h>
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
#include <cstdlib>
#include <ctime>

#include <windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <Shellapi.h>
#include <Shlobj.h>
#include <dbt.h>
#include <Winhttp.h>

// TODO:
// 2) Icons
// 3) Unmount when webdav server unexpectedly stops (defensive coding)

enum class DriveLetter {
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
};

struct CloseHandleDeleter {
  void operator()(HANDLE a) {
    auto ret = CloseHandle(a);
    if (!ret) throw std::runtime_error("couldn't free!");
  }
};

class ManagedThreadHandle :
  public lockbox::ManagedResource<HANDLE, CloseHandleDeleter> {
public:
  ManagedThreadHandle(HANDLE a) :
    lockbox::ManagedResource<HANDLE, CloseHandleDeleter>(std::move(a)) {}

  operator bool() const {
    return (bool) get();
  }
};

struct DestroyMenuDeleter {
  void operator()(HMENU a) {
    auto ret = DestroyMenu(a);
    if (!ret) throw std::runtime_error("couldn't free!");
  }
};

typedef lockbox::ManagedResource<HMENU, DestroyMenuDeleter> ManagedMenuHandle;

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
  UINT TASKBAR_CREATED_MSG;
  std::vector<ManagedThreadHandle> threads_to_join;
  UINT LOCKBOX_DUPLICATE_INSTANCE_MSG;
  HWND last_foreground_window;
  bool popup_menu_is_open;
};

struct ServerThreadParams {
  HWND hwnd;
  std::shared_ptr<encfs::FsIO> native_fs;
  encfs::Path encrypted_directory_path;
  encfs::EncfsConfig encfs_config;
  encfs::SecureMem password;
  std::string mount_name;
};

// constants
const unsigned MOUNT_RETRY_ATTEMPTS = 10;
const WORD IDC_STATIC = ~0;
const WORD IDPASSWORD = 200;
const WORD IDCONFIRMPASSWORD = 201;
const auto MAX_PASS_LEN = 256;
const wchar_t TRAY_ICON_TOOLTIP[] = PRODUCT_NAME_W;
const wchar_t MAIN_WINDOW_CLASS_NAME[] = L"lockbox_tray_icon";
const UINT_PTR STOP_RELEVANT_DRIVE_THREADS_TIMER_ID = 0;
const UINT_PTR CHECK_STOPPED_THREADS_TIMER_ID = 1;

const auto APP_BASE = (UINT) (6 + WM_APP);
const auto LOCKBOX_MOUNT_DONE_MSG = APP_BASE;
const auto LOCKBOX_MOUNT_OVER_MSG = APP_BASE + 1;
const auto LOCKBOX_TRAY_ICON_MSG = APP_BASE + 2;
const auto LOCKBOX_TRAY_ICON_MSG_2 = APP_BASE + 3;

const auto LOCKBOX_TRAY_ICON_ID = 1;

const auto MENU_MOUNT_IDX_BASE = (UINT) 1;
const auto MENU_BASE = MENU_MOUNT_IDX_BASE + 26;
const auto MENU_CREATE_ID = MENU_BASE + 0;
const auto MENU_QUIT_ID = MENU_BASE + 1;
const auto MENU_DEBUG_ID = MENU_BASE + 2;
const auto MENU_GETSRCCODE_ID = MENU_BASE + 3;
const auto MENU_TEST_BUBBLE_ID = MENU_BASE + 4;

const wchar_t LOCKBOX_SINGLE_APP_INSTANCE_MUTEX_NAME[] =
  L"LockboxAppMutex";
const wchar_t LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME[] =
  L"LockboxAppSharedProcessId";
const wchar_t LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME[] =
  L"LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME";
const wchar_t TASKBAR_CREATED_MESSAGE_NAME[] =
  L"TaskbarCreated";
const char LOCKBOX_TRAY_ICON_WELCOME_TITLE[] =
  PRODUCT_NAME_A " is now Running!";
const char LOCKBOX_TRAY_ICON_WELCOME_MSG[] =
  "If you need to use "
  PRODUCT_NAME_A
  ", just right-click on this icon.";

// DriveLetter helpers
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

// ManagedThreadHandle helpers
ManagedThreadHandle create_managed_thread_handle(HANDLE a) {
  return ManagedThreadHandle(a);
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

UINT
mount_idx_to_menu_id(unsigned idx) {
  return idx + MENU_MOUNT_IDX_BASE;
}

unsigned
menu_id_to_mount_idx(UINT id) {
  assert(id >= MENU_MOUNT_IDX_BASE);
  return id - MENU_MOUNT_IDX_BASE;
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
bool
wait_on_handle_with_message_loop(HANDLE ptr) {
  while (true) {
    auto ret =
      MsgWaitForMultipleObjects(1, &ptr,
                                FALSE, INFINITE, QS_ALLINPUT);
    if (ret == WAIT_OBJECT_0) return true;
    else if (ret == WAIT_OBJECT_0 + 1) {
      // we have a message - peek and dispatch it
      MSG msg;
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          PostQuitMessage(msg.wParam);
          return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
    // random error
    else return false;
  }
}

static
void
queue_wait_for_thread_to_die(HWND hwnd, WindowData & wd,
                             ManagedThreadHandle thread_handle) {
  wd.threads_to_join.push_back(std::move(thread_handle));
  auto success =
    SetTimer(hwnd, CHECK_STOPPED_THREADS_TIMER_ID, 0, NULL);
  if (!success) throw std::runtime_error("failed to add timer");
}

static
void
bubble_msg(HWND lockbox_main_window,
           const std::string title, const std::string &msg);

static
std::vector<MountDetails>::iterator
stop_drive(HWND lockbox_main_window,
           WindowData & wd,
           std::vector<MountDetails>::iterator it) {
  // remove mount from list (even if thread hasn't died)
  auto md = std::move(*it);
  it = wd.mounts.erase(it);

  // then stop thread
  auto success =
    w32util::run_async_void(stop_drive_thread,
                            md.thread_handle.get(),
                            md.listen_port);
  // if not success, it was a quit message
  if (!success) return it;

  // asynchronously wait for thread to stop
  queue_wait_for_thread_to_die(lockbox_main_window, wd,
                               std::move(md.thread_handle));

  // TODO: this might be too spammy if multiple drives have been
  // unmounted
  bubble_msg(lockbox_main_window,
             "Success",
             "You've successfully stopped \"" + md.name +
             ".\"");

  return it;
}

static
void
stop_relevant_drive_threads(HWND lockbox_main_window, WindowData & wd) {
  assert(!wd.popup_menu_is_open);

  auto & mounts = wd.mounts;
  DWORD last_bitmask = 0;

  while (true) {
    auto drive_bitmask = GetLogicalDrives();
    if (!drive_bitmask) {
      throw std::runtime_error("Error while calling GetLogicalDrives");
    }

    if (drive_bitmask == last_bitmask) return;

    for (auto it = mounts.begin(); it != mounts.end();) {
      if (!((drive_bitmask >> (int) it->drive_letter) & 0x1)) {
        it = stop_drive(lockbox_main_window, wd, it);
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
    bi.lpszTitle = L"Select Location for new " ENCRYPTED_STORAGE_NAME_W;
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

bool
open_src_code(HWND owner) {
  auto ret_shell2 =
    (int) ShellExecuteW(owner, L"open",
                        L"http://github.com/rianhunter/lockbox_app",
                        NULL, NULL, SW_SHOWNORMAL);
  return ret_shell2 > 32;
}

CALLBACK
INT_PTR
get_password_dialog_proc(HWND hwnd, UINT Message,
                         WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG:
    w32util::center_window_in_monitor(hwnd);
    w32util::set_default_dialog_font(hwnd);
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
  case WM_DESTROY:
    w32util::cleanup_default_dialog_font(hwnd);
    // we don't actually handle this
    return FALSE;
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
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Enter Your Password",
                              0, 0, 100, 66),
                   {
                     CText("Enter Your Password:", IDC_STATIC,
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
INT_PTR
confirm_new_encrypted_container_proc(HWND hwnd, UINT Message,
                                     WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG: {
    w32util::center_window_in_monitor(hwnd);
    w32util::set_default_dialog_font(hwnd);

    // set focus to create button
    auto ok_button_hwnd = GetDlgItem(hwnd, IDOK);
    if (!ok_button_hwnd) throw std::runtime_error("Couldn't get create button");
    PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM) ok_button_hwnd, TRUE);
    return TRUE;
  }

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

  case WM_DESTROY:
    w32util::cleanup_default_dialog_font(hwnd);
    // we don't actually handle this
    return FALSE;

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
  os << "The folder you selected:\r\n\r\n    \"" <<
    (const std::string &) encrypted_directory_path <<
    ("\"\r\n\r\ndoes not appear to store a " ENCRYPTED_STORAGE_NAME_A ". "
     "Would you like to create a new one there?");
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
  const unit_t VERT_MARGIN = 8;
  const unit_t HORIZ_MARGIN = 8;

  const unit_t TOP_MARGIN = VERT_MARGIN;
  const unit_t BOTTOM_MARGIN = VERT_MARGIN;
  const unit_t LEFT_MARGIN = HORIZ_MARGIN;
  const unit_t RIGHT_MARGIN = HORIZ_MARGIN;

  const unit_t MIDDLE_MARGIN = 4;

  const unit_t TEXT_WIDTH = 160;
  const unit_t TEXT_HEIGHT = FONT_HEIGHT * 6;
  const unit_t BUTTON_WIDTH = 44;
  const unit_t BUTTON_HEIGHT = 14;
  const unit_t BUTTON_SPACING = 4;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "No " ENCRYPTED_STORAGE_NAME_A " Found",
                              0, 0, DIALOG_WIDTH,
                              TOP_MARGIN + TEXT_HEIGHT +
                              MIDDLE_MARGIN + BUTTON_HEIGHT +
                              BOTTOM_MARGIN),
                   {
                     LText(std::move(dialog_text), IDC_STATIC,
                           LEFT_MARGIN, TOP_MARGIN,
                           TEXT_WIDTH, TEXT_HEIGHT),
                     PushButton("&Cancel", IDCANCEL,
                                DIALOG_WIDTH - RIGHT_MARGIN -
                                BUTTON_WIDTH,
                                TOP_MARGIN + TEXT_HEIGHT + MIDDLE_MARGIN,
                                BUTTON_WIDTH, BUTTON_HEIGHT),
                     DefPushButton("C&reate", IDOK,
                                   DIALOG_WIDTH - RIGHT_MARGIN -
                                   BUTTON_WIDTH - BUTTON_SPACING -
                                   BUTTON_WIDTH,
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
INT_PTR
get_new_password_dialog_proc(HWND hwnd, UINT Message,
                             WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG: {
    w32util::center_window_in_monitor(hwnd);
    SendDlgItemMessage(hwnd, IDPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    SendDlgItemMessage(hwnd, IDCONFIRMPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    w32util::set_default_dialog_font(hwnd);
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
  case WM_DESTROY:
    w32util::cleanup_default_dialog_font(hwnd);
    // we don't actually handle this
    return FALSE;
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

  const unit_t VERT_MARGIN = 8;
  const unit_t HORIZ_MARGIN = 8;

  const unit_t TOP_MARGIN = VERT_MARGIN;
  const unit_t BOTTOM_MARGIN = VERT_MARGIN;
  const unit_t LEFT_MARGIN = HORIZ_MARGIN;
  const unit_t RIGHT_MARGIN = HORIZ_MARGIN;

  const unit_t MIDDLE_MARGIN = FONT_HEIGHT;

  const unit_t TEXT_WIDTH = 160;
  const unit_t TEXT_HEIGHT = FONT_HEIGHT;

  const unit_t PASS_LABEL_CHARS = strlen("Confirm Password:");

  const unit_t PASS_LABEL_WIDTH = FONT_WIDTH * PASS_LABEL_CHARS;
  const unit_t PASS_LABEL_HEIGHT = FONT_HEIGHT;

  const unit_t DIALOG_WIDTH = PASS_LABEL_WIDTH + 125;

  const unit_t PASS_LABEL_SPACE = 0;
  const unit_t PASS_LABEL_VOFFSET = 2;
  const unit_t PASS_MARGIN = FONT_HEIGHT / 2;

  const unit_t PASS_ENTRY_LEFT_MARGIN =
    LEFT_MARGIN + PASS_LABEL_WIDTH + PASS_LABEL_SPACE;
  const unit_t PASS_ENTRY_WIDTH = DIALOG_WIDTH - RIGHT_MARGIN -
    PASS_ENTRY_LEFT_MARGIN;
  const unit_t PASS_ENTRY_HEIGHT = 12;

  const unit_t BUTTON_WIDTH = 44;
  const unit_t BUTTON_HEIGHT = 14;
  const unit_t BUTTON_SPACING = 4;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Create " ENCRYPTED_STORAGE_NAME_A,
                              0, 0, DIALOG_WIDTH,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN +
                              PASS_ENTRY_HEIGHT + PASS_MARGIN +
                              PASS_ENTRY_HEIGHT + MIDDLE_MARGIN +
                              BUTTON_HEIGHT + BOTTOM_MARGIN),
                   {
                     LText(("Enter a password for your new "
                            ENCRYPTED_STORAGE_NAME_A
                            "."),
                           IDC_STATIC,
                           LEFT_MARGIN, TOP_MARGIN,
                           TEXT_WIDTH, TEXT_HEIGHT),
                     LText("New Password:", IDC_STATIC,
                           LEFT_MARGIN,
                           TOP_MARGIN +
                           TEXT_HEIGHT + MIDDLE_MARGIN + PASS_LABEL_VOFFSET,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDPASSWORD,
                              PASS_ENTRY_LEFT_MARGIN,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     LText("Confirm Password:", IDC_STATIC,
                           LEFT_MARGIN,
                           TOP_MARGIN +
                           TEXT_HEIGHT + MIDDLE_MARGIN +
                           PASS_ENTRY_HEIGHT + PASS_MARGIN + PASS_LABEL_VOFFSET,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDCONFIRMPASSWORD,
                              PASS_ENTRY_LEFT_MARGIN,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN +
                              PASS_ENTRY_HEIGHT + PASS_MARGIN,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     PushButton("&Cancel", IDCANCEL,
                                DIALOG_WIDTH - RIGHT_MARGIN - BUTTON_WIDTH,
                                TOP_MARGIN +
                                TEXT_HEIGHT + MIDDLE_MARGIN +
                                PASS_ENTRY_HEIGHT + PASS_MARGIN +
                                PASS_ENTRY_HEIGHT + MIDDLE_MARGIN,
                                BUTTON_WIDTH, BUTTON_HEIGHT),
                     DefPushButton("&OK", IDOK,
                                   DIALOG_WIDTH -
                                   RIGHT_MARGIN - BUTTON_WIDTH -
                                   BUTTON_SPACING - BUTTON_WIDTH,
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
    PostMessage(params->hwnd, LOCKBOX_MOUNT_DONE_MSG,
                1, listen_port);
    sent_signal = true;
  };

  lockbox::run_lockbox_webdav_server(std::move(enc_fs),
                                     std::move(params->encrypted_directory_path),
                                     ip_addr,
                                     listen_port,
                                     std::move(params->mount_name),
                                     our_callback);

  auto sig = (sent_signal)
    ? LOCKBOX_MOUNT_OVER_MSG
    : LOCKBOX_MOUNT_DONE_MSG;

  PostMessage(params->hwnd, sig, 0, 0);

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

    const auto use_case_safe_filename_encoding = true;
    maybe_encfs_config =
      w32util::modal_call(owner,
                          "Creating New Configuration...",
                          "Creating new configuration...",
                          encfs::create_paranoid_config,
                          *maybe_password,
                          use_case_safe_filename_encoding);
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
    owner,
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
                                              ("Starting New "
                                               ENCRYPTED_STORAGE_NAME_A
                                               "..."),
                                              ("Starting new "
                                               ENCRYPTED_STORAGE_NAME_A
                                               "..."),
                                              LOCKBOX_MOUNT_DONE_MSG);
  // TODO: kill thread
  if (!msg_ptr) return opt::nullopt;

  assert(msg_ptr->message == LOCKBOX_MOUNT_DONE_MSG);

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
                                    ("Starting New "
                                     ENCRYPTED_STORAGE_NAME_A
                                     "..."),
                                    ("Starting new "
                                     ENCRYPTED_STORAGE_NAME_A
                                     "..."));
    // TODO: kill thread
    if (!dialog_wnd) return opt::nullopt;
    auto _destroy_dialog_wnd =
      lockbox::create_destroyer(dialog_wnd, DestroyWindow);

    // disable window
    EnableWindow(owner, FALSE);

    // just mount the file system using the console command

    std::ostringstream os;
    os << "use " << drive_letter <<
      // we wrap the url in quotes since it could have a space, etc.
      // we don't urlencode it because windows will do that for us
      ": \"http://localhost:" << listen_port << "/" << mount_name << "\"";
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
  for (i = 0; i < MOUNT_RETRY_ATTEMPTS; ++i) {
    // this is not that bad, we were just opening the windows
    if (!open_mount(owner, md)) {
      log_error("opening mount failed, trying again...: %s",
                w32util::last_error_message().c_str());
      Sleep(0);
    }
    else break;
  }

  if (i == MOUNT_RETRY_ATTEMPTS) {
    bubble_msg(owner,
               "Success",
               "You've successfully started \"" + md.name +
               ".\" Right-click here to open it.");
  }
  else {
    bubble_msg(owner,
               "Success",
               "You've successfully started \"" + md.name +
               ".\"");
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
  mif.fType = MFT_STRING;
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

static
bool
alert_of_popup_if_we_have_one(HWND lockbox_main_window, const WindowData & wd,
                              bool beep = true) {
  auto child = GetWindow(lockbox_main_window, GW_ENABLEDPOPUP);
  if (child) {
    SetForegroundWindow(child);

    if (beep && child == wd.last_foreground_window) {
      DWORD flash_count;
      auto success =
        SystemParametersInfoW(SPI_GETFOREGROUNDFLASHCOUNT,
                              0, &flash_count, 0);
      if (!success) throw w32util::windows_error();

      FLASHWINFO finfo;
      lockbox::zero_object(finfo);
      finfo.cbSize = sizeof(finfo);
      finfo.hwnd = child;
      finfo.dwFlags = FLASHW_ALL;
      finfo.uCount = flash_count;
      finfo.dwTimeout = 66;
    
      FlashWindowEx(&finfo);
      MessageBeep(MB_OK); 
    }
  }

  return (bool) child;
}

WINAPI
bool
open_create_mount_dialog(HWND lockbox_main_window, WindowData & wd) {
  // only run this dialog if we don't already have a dialog
  assert(!GetWindow(lockbox_main_window, GW_ENABLEDPOPUP));
  auto md = mount_encrypted_folder_dialog(lockbox_main_window, wd.native_fs);
  if (md) wd.mounts.push_back(std::move(*md));
  return (bool) md;
}

bool
unmount_drive(HWND hwnd, const MountDetails & mount) {
  std::ostringstream os;
  os << "use " << mount.drive_letter <<  ": /delete";
  auto ret_shell1 = (int) ShellExecuteW(hwnd, L"open",
                                        L"c:\\windows\\system32\\net.exe",
                                        w32util::widen(os.str()).c_str(),
                                        NULL, SW_HIDE);
  return ret_shell1 > 32;
}

void
unmount_and_stop_drive(HWND hwnd, WindowData & wd, size_t mount_idx) {
  assert(!wd.popup_menu_is_open);

  auto mount_p = wd.mounts.begin() + mount_idx;

  // first unmount drive
  const auto success_unmount = unmount_drive(hwnd, *mount_p);
  if (!success_unmount) {
    throw std::runtime_error("Unmount error!");
  }

  // now remove mount from list
  stop_drive(hwnd, wd, mount_p);
}

static
BOOL
SetClientSize(HWND hwnd, bool set_pos,
              int x, int y,
              int w, int h) {
  RECT a;
  a.left = 0;
  a.top = 0;
  a.bottom = h;
  a.right = w;
  DWORD style = GetWindowLongPtr(hwnd, GWL_STYLE);
  if (!style) return FALSE;
  
  DWORD ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (!ex_style) return FALSE;

  auto success = AdjustWindowRectEx(&a, style, FALSE, ex_style);
  if (!success) return FALSE;

  return SetWindowPos(hwnd, NULL, x, y,
                      a.right - a.left,
                      a.bottom - a.top,
                      SWP_NOACTIVATE |
                      (set_pos ? 0 : SWP_NOMOVE));
}

static
BOOL
SetClientSizeInLogical(HWND hwnd, bool set_pos,
                       int x, int y,
                       int w, int h) {
  auto parent_hwnd = GetParent(hwnd);
  if (!parent_hwnd) parent_hwnd = GetDesktopWindow();
  if (!parent_hwnd) return FALSE;

  auto parent_hdc = GetDC(parent_hwnd);
  if (!parent_hdc) return FALSE;
  auto _release_dc_1 =
    lockbox::create_deferred(ReleaseDC, parent_hwnd, parent_hdc);

  POINT p1 = {x, y};
  auto success_1 = LPtoDP(parent_hdc, &p1, 1);
  if (!success_1) return FALSE;

  auto dc = GetDC(hwnd);
  if (!dc) return FALSE;
  auto _release_dc_2 = lockbox::create_deferred(ReleaseDC, hwnd, dc);

  POINT p2 = {w, h};
  auto success_2 = LPtoDP(dc, &p1, 1);
  if (!success_2) return FALSE;

  return SetClientSize(hwnd, set_pos,
                       p1.x, p1.y,
                       p2.x, p2.y);
}

enum {
  IDCBLURB = 100,
  IDCGETSOURCECODE,
  IDCCREATELOCKBOX,
};

CALLBACK
INT_PTR
about_dialog_proc(HWND hwnd, UINT Message,
                  WPARAM wParam, LPARAM /*lParam*/) {

  switch (Message) {
  case WM_INITDIALOG: {
    w32util::set_default_dialog_font(hwnd);

    // position everything
    typedef unsigned unit_t;

    // compute size of about string
    auto text_hwnd = GetDlgItem(hwnd, IDCBLURB);
    if (!text_hwnd) throw w32util::windows_error();

    const unit_t BLURB_TEXT_WIDTH = 350;

    const unit_t BUTTON_WIDTH_SRCCODE_DLG = 55;
    const unit_t BUTTON_WIDTH_CREATELB_DLG = 100;
    const unit_t BUTTON_HEIGHT_DLG = 14;

    RECT r;
    r.left = BUTTON_WIDTH_SRCCODE_DLG;
    r.right = BUTTON_WIDTH_CREATELB_DLG;
    r.top = BUTTON_HEIGHT_DLG;
    auto success_map = MapDialogRect(hwnd, &r);
    if (!success_map) throw w32util::windows_error();

    const unit_t BUTTON_WIDTH_SRCCODE = r.left;
    const unit_t BUTTON_WIDTH_CREATELB = r.right;
    const unit_t BUTTON_HEIGHT = r.top;

    const unit_t BUTTON_SPACING = 8;

    auto blurb_text = w32util::widen(LOCKBOX_ABOUT_BLURB);

    auto dc = GetDC(text_hwnd);
    if (!dc) throw w32util::windows_error();
    auto _release_dc_1 = lockbox::create_deferred(ReleaseDC, text_hwnd, dc);

    auto hfont = (HFONT) SendMessage(text_hwnd, WM_GETFONT, 0, 0);
    if (!hfont) return FALSE;
    auto font_dc = SelectObject(dc, hfont);
    if (!font_dc) throw w32util::windows_error();

    auto w = BLURB_TEXT_WIDTH;
    RECT rect;
    lockbox::zero_object(rect);
    rect.right = w;
    auto h = DrawText(dc, blurb_text.data(), blurb_text.size(), &rect,
                      DT_CALCRECT | DT_NOCLIP | DT_LEFT | DT_WORDBREAK);
    if (!h) throw w32util::windows_error();
    w = rect.right;

    auto margin = 8;
    const unit_t DIALOG_WIDTH = margin + w + margin;
    const unit_t DIALOG_HEIGHT =
      margin + h + margin + BUTTON_HEIGHT + margin;

    // set up text window
    auto success_set_wtext = SetWindowText(text_hwnd, blurb_text.c_str());
    if (!success_set_wtext) throw w32util::windows_error();
    auto set_client_area_1 = SetClientSizeInLogical(text_hwnd, true,
                                                    margin, margin,
                                                    w, h);
    if (!set_client_area_1) throw w32util::windows_error();

    // create "create lockbox" button
    auto create_lockbox_hwnd = GetDlgItem(hwnd, IDCCREATELOCKBOX);
    if (!create_lockbox_hwnd) throw w32util::windows_error();

    SetClientSizeInLogical(create_lockbox_hwnd, true,
                           DIALOG_WIDTH -
                           margin - BUTTON_WIDTH_SRCCODE -
                           BUTTON_SPACING - BUTTON_WIDTH_CREATELB,
                           margin + h + margin,
                           BUTTON_WIDTH_CREATELB, BUTTON_HEIGHT);

    // set focus on create lockbox button
    PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM) create_lockbox_hwnd, TRUE);

    // create "get source code" button
    auto get_source_code_hwnd = GetDlgItem(hwnd, IDCGETSOURCECODE);
    if (!get_source_code_hwnd) throw w32util::windows_error();

    SetClientSizeInLogical(get_source_code_hwnd, true,
                           DIALOG_WIDTH -
                           margin - BUTTON_WIDTH_SRCCODE,
                           margin + h + margin,
                           BUTTON_WIDTH_SRCCODE, BUTTON_HEIGHT);

    auto set_client_area_2 = SetClientSizeInLogical(hwnd, true, 0, 0,
                                                    DIALOG_WIDTH,
                                                    DIALOG_HEIGHT);
    if (!set_client_area_2) throw w32util::windows_error();

    w32util::center_window_in_monitor(hwnd);

    return TRUE;
  }

  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDCGETSOURCECODE: {
      open_src_code(hwnd);
      return TRUE;
    }
    case IDCCREATELOCKBOX: case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) LOWORD(wParam));
      return TRUE;
    }
    default: return FALSE;
    }

    // not reached
    assert(false);
  }

  case WM_DESTROY: {
    w32util::cleanup_default_dialog_font(hwnd);
    // we don't actually handle this
    return FALSE;
  }

  default: return FALSE;
  }

  // not reached
  assert(false);
}

WINAPI
static
INT_PTR
about_dialog(HWND hwnd) {
  using namespace w32util;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              ("Welcome to "
                               PRODUCT_NAME_A
                               "!"),
                              0, 0, 500, 500),
                   {
                     LText("", IDCBLURB,
                           0, 0, 0, 0),
                     PushButton("&Get Source Code", IDCGETSOURCECODE,
                                0, 0, 0, 0),
                     DefPushButton(("&Start or Create a "
                                    ENCRYPTED_STORAGE_NAME_A),
                                   IDCCREATELOCKBOX,
                                   0, 0, 0, 0),
                   }
                   );

  return
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      hwnd, about_dialog_proc);
}

static
void
perform_default_lockbox_action(HWND lockbox_main_window, WindowData & wd) {
  auto child = alert_of_popup_if_we_have_one(lockbox_main_window, wd, false);
  if (child) return;

  // if there is an active mount, open the folder
  // otherwise allow the user to make a new mount
  if (wd.mounts.empty()) open_create_mount_dialog(lockbox_main_window, wd);
  else {
    auto success = open_mount(lockbox_main_window, wd.mounts.front());
    if (!success) {
      log_error("Error while opening mount... %s: %s",
                std::to_string(wd.mounts.front().drive_letter).c_str(),
                w32util::last_error_message().c_str());
    }
  }
}

static
void
bubble_msg(HWND lockbox_main_window,
           const std::string title, const std::string &msg) {
  NOTIFYICONDATAW icon_data;
  lockbox::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = lockbox_main_window;
  icon_data.uID = LOCKBOX_TRAY_ICON_ID;
  icon_data.uFlags = NIF_INFO;
  icon_data.uVersion = NOTIFYICON_VERSION;

  auto wmsg = w32util::widen(msg);
  if (wmsg.size() >= sizeof(icon_data.szInfo)) {
    throw std::runtime_error("msg is too long!");
  }
  wmemcpy(icon_data.szInfo, wmsg.c_str(), wmsg.size() + 1);
  icon_data.uTimeout = 0; // let it be the minimum

  auto wtitle = w32util::widen(title);
  if (wtitle.size() >= sizeof(icon_data.szInfoTitle)) {
    throw std::runtime_error("title is too long!");
  }
  wmemcpy(icon_data.szInfoTitle, wtitle.c_str(), wtitle.size() + 1);

  icon_data.dwInfoFlags = NIIF_NONE;

  auto success = Shell_NotifyIconW(NIM_MODIFY, &icon_data);
  if (!success) throw w32util::windows_error();
}

ManagedMenuHandle
create_lockbox_menu(const WindowData & wd, bool is_control_pressed) {
  // okay our menu is like this
  // [ Lockbox A ]
  // [ Lockbox B ]
  // ...
  // [ ------- ]
  // Open or create a new Lockbox
  // Get Source Code
  // Quit
  // [ DebugBreak ]
  // [ Test Bubble ]

  auto menu_handle = CreatePopupMenu();
  if (!menu_handle) throw std::runtime_error("CreatePopupMenu");
  auto to_ret = ManagedMenuHandle(menu_handle);

  const auto action = is_control_pressed
    ? std::string("Stop")
    : std::string("Open");

  UINT idx = 0;
  for (const auto & md : wd.mounts) {
    append_string_menu_item(menu_handle,
                            idx == 0,
                            (action + " \"" + md.name + "\" (" +
                             std::to_string(md.drive_letter) + ":)"),
                            mount_idx_to_menu_id(idx));
    ++idx;
  }

  // add separator
  if (!wd.mounts.empty()) {
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

  // add create action
  append_string_menu_item(menu_handle,
                          wd.mounts.empty(),
                          "Start or create a " ENCRYPTED_STORAGE_NAME_A,
                          MENU_CREATE_ID);

  // add get source code action
  append_string_menu_item(menu_handle,
                          false,
                          "Get source code",
                          MENU_GETSRCCODE_ID);

  // add quit action
  append_string_menu_item(menu_handle,
                          false,
                          "Exit",
                          MENU_QUIT_ID);

#ifndef NDEBUG
  // add breakpoint action
  append_string_menu_item(menu_handle,
                          false,
                          "DebugBreak",
                          MENU_DEBUG_ID);

  // add breakpoint action
  append_string_menu_item(menu_handle,
                          false,
                          "Test Bubble",
                          MENU_TEST_BUBBLE_ID);
#endif

  return std::move(to_ret);
}

static
void
add_tray_icon(HWND lockbox_main_window) {
  NOTIFYICONDATAW icon_data;
  lockbox::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = lockbox_main_window;
  icon_data.uID = LOCKBOX_TRAY_ICON_ID;
  icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon_data.uCallbackMessage = LOCKBOX_TRAY_ICON_MSG;
  icon_data.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  icon_data.uVersion = NOTIFYICON_VERSION;
  memcpy(icon_data.szTip, TRAY_ICON_TOOLTIP, sizeof(TRAY_ICON_TOOLTIP));

  auto success = Shell_NotifyIconW(NIM_ADD, &icon_data);
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
}

#define NO_FALLTHROUGH(c) assert(false); case c

CALLBACK
LRESULT
main_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const auto wd = (WindowData *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
  const auto TASKBAR_CREATED_MSG = wd ? wd->TASKBAR_CREATED_MSG : 0;
  const auto LOCKBOX_DUPLICATE_INSTANCE_MSG =
    wd ? wd->LOCKBOX_DUPLICATE_INSTANCE_MSG : 0;
  switch(msg){
    NO_FALLTHROUGH(LOCKBOX_MOUNT_OVER_MSG): {
      // TODO: handle this
      return 0;
    }

    NO_FALLTHROUGH(WM_TIMER): {
      if (wParam == STOP_RELEVANT_DRIVE_THREADS_TIMER_ID) {
        if (wd->popup_menu_is_open) {
          // delay message until menu is closed
          auto success =
            SetTimer(hwnd, STOP_RELEVANT_DRIVE_THREADS_TIMER_ID, 0, NULL);
          if (!success) throw w32util::windows_error();
        } else if (!wd->is_stopping) {
          wd->is_stopping = true;
          // if this throws an exception, our app is going to close
          stop_relevant_drive_threads(hwnd, *wd);
          wd->is_stopping = false;
        }
        return 0;
      }
      else if (wParam == CHECK_STOPPED_THREADS_TIMER_ID) {
        // TODO: implement
        UNUSED(wait_on_handle_with_message_loop);
        wd->threads_to_join.clear();
        return 0;
      }
      else {
        // we didn't process this timer
        return -1;
      }
    }

    NO_FALLTHROUGH(WM_DEVICECHANGE): {
      // NB: stop the thread on a different message
      //     since the OS blocks on the return from this message dispatch
      //     to actually unmount the drive
      if (wParam == DBT_DEVICEREMOVECOMPLETE) {
        auto success =
          SetTimer(hwnd, STOP_RELEVANT_DRIVE_THREADS_TIMER_ID, 0, NULL);
        // TODO: we don't yet handle this gracefully
        if (!success) throw w32util::windows_error();
      }
      return TRUE;
    }

    NO_FALLTHROUGH(WM_CREATE): {
      // set application state (WindowData) in main window
      const auto wd = (WindowData *) ((LPCREATESTRUCT) lParam)->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) wd);

      // add tray icon
      add_tray_icon(hwnd);

      // open about dialog
      auto about_action = about_dialog(hwnd);

      // okay now do action
      bool show_bubble = true;
      if (about_action == IDCCREATELOCKBOX) {
        show_bubble = !open_create_mount_dialog(hwnd, *wd);
      }

      if (show_bubble) {
        bubble_msg(hwnd,
                   LOCKBOX_TRAY_ICON_WELCOME_TITLE,
                   LOCKBOX_TRAY_ICON_WELCOME_MSG);
      }

      // return -1 on failure, CreateWindow* will return NULL
      return 0;
    }

    NO_FALLTHROUGH(LOCKBOX_TRAY_ICON_MSG): {
      switch (lParam) {
        NO_FALLTHROUGH(1029): {
          log_debug("User clicked balloon");
          break;
        }

        NO_FALLTHROUGH(WM_MOUSEMOVE): {
          wd->last_foreground_window = GetForegroundWindow();
          break;
        }

        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
          alert_of_popup_if_we_have_one(hwnd, *wd);
          break;
        }

        NO_FALLTHROUGH(WM_LBUTTONDBLCLK): {
          perform_default_lockbox_action(hwnd, *wd);
          break;
        }

        // NB: Must popup menu on WM_RBUTTONUP presumably because the menu
        //     closes when it gets when clicked off WM_RBUTTONDOWN 
        //     (otherwise menu will activate and then immediately close)
        NO_FALLTHROUGH(WM_RBUTTONUP): {
          // repost a top-level message to the window
          // since sometimes these messages come while the menu is still
          // being "tracked" (this is a workaround for a bug in windows)
          POINT xypos;
          auto success_pos = GetCursorPos(&xypos);
          if (!success_pos) throw std::runtime_error("GetCursorPos");

          const auto is_control_pressed =
            GetKeyState(VK_CONTROL) >>
            (sizeof(GetKeyState(VK_CONTROL)) * 8 - 1);

          PostMessage(hwnd, LOCKBOX_TRAY_ICON_MSG_2,
                      (LPARAM) is_control_pressed,
                      MAKELPARAM(xypos.x, xypos.y));
          break;
        }

      default: break;
      }

      // Always return 0 for all tray icon messages
      return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    NO_FALLTHROUGH(LOCKBOX_TRAY_ICON_MSG_2): {
      {
        //only do the action if we don't have a dialog up
        auto child = alert_of_popup_if_we_have_one(hwnd, *wd, false);
        if (child) return 0;
      }

      if (wd->popup_menu_is_open) return 0;

      try {
        auto is_control_pressed = (bool) wParam;
        auto menu_handle = create_lockbox_menu(*wd, is_control_pressed);

        // NB: have to do this so the menu disappears
        // when you click out of it
        SetForegroundWindow(hwnd);

        wd->popup_menu_is_open = true;
        SetLastError(0);
        auto selected =
          (UINT) TrackPopupMenu(menu_handle.get(),
                                TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                                TPM_RIGHTBUTTON |
                                TPM_NONOTIFY | TPM_RETURNCMD,
                                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                0, hwnd, NULL);
        wd->popup_menu_is_open = false;

        if (!selected) throw std::runtime_error("TrackPopupMenu");

        // according to MSDN, we must force a "task switch"
        // this is so that if the user attempts to open the
        // menu while the menu is open, it reopens instead
        // of disappearing
        PostMessage(hwnd, WM_NULL, 0, 0);

        switch (selected) {
        case 0:
          // menu was canceled
          break;
        case MENU_QUIT_ID: {
          DestroyWindow(hwnd);
          break;
        }
        case MENU_CREATE_ID: {
          open_create_mount_dialog(hwnd, *wd);
          break;
        }
        case MENU_DEBUG_ID: {
          DebugBreak();
          break;
        }
        case MENU_GETSRCCODE_ID: {
          open_src_code(hwnd);
          break;
        }
        case MENU_TEST_BUBBLE_ID: {
          bubble_msg(hwnd, "Short Title",
                     "Very long message full of meaningful info that you "
                     "will find very interesting because you love to read "
                     "tray icon bubbles. Don't you? Don't you?!?!");
          break;
        }
        default: {
          const auto selected_mount_idx = menu_id_to_mount_idx(selected);
          assert(selected_mount_idx < wd->mounts.size());
          if (is_control_pressed) {
            unmount_and_stop_drive(hwnd, *wd, selected_mount_idx);
          }
          else {
            auto success = open_mount(hwnd, wd->mounts[selected_mount_idx]);
            if (!success) throw std::runtime_error("open_mount");
          }
          break;
        }
        }
      }
      catch (const std::exception & err) {
        log_error("Error while doing: \"%s\": %s (%lu)",
                  err.what(),
                  w32util::last_error_message().c_str(),
                  GetLastError());
      }

      return 0;
    }

    NO_FALLTHROUGH(WM_CLOSE): {
      DestroyWindow(hwnd);
      return 0;
    }

    NO_FALLTHROUGH(WM_DESTROY): {
      // remove tray icon
      NOTIFYICONDATA icon_data;
      lockbox::zero_object(icon_data);
      icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
      icon_data.hWnd = hwnd;
      icon_data.uID = LOCKBOX_TRAY_ICON_ID;
      icon_data.uVersion = NOTIFYICON_VERSION;

      Shell_NotifyIcon(NIM_DELETE, &icon_data);

      // quit application
      PostQuitMessage(0);
      return 0;
    }

  default:
    if (msg == TASKBAR_CREATED_MSG) {
      // re-add tray icon
      add_tray_icon(hwnd);
    }
    else if (msg == LOCKBOX_DUPLICATE_INSTANCE_MSG){
      // do the current default action
      perform_default_lockbox_action(hwnd, *wd);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  /* not reached, there is no default return code */
  assert(false);
}

static
void
exit_if_not_single_app_instance(std::function<int(DWORD)> on_exit) {
  // NB: we abort() instead of throwing exceptions
  //     we exit() instead of returning a boolean
  //     this function must ensure a single app instance and
  //     this is the best contract for that, otherwise
  //     a user could misuse this function and potentially continue
  //     executing

  SetLastError(0);
  auto mutex_handle =
    CreateMutexW(NULL, TRUE, LOCKBOX_SINGLE_APP_INSTANCE_MUTEX_NAME);
  switch (GetLastError()) {
  case ERROR_SUCCESS: {
    // write our process id in a shared memory
    auto mapping_handle =
      CreateFileMapping(INVALID_HANDLE_VALUE,
                        NULL, PAGE_READWRITE,
                        0, sizeof(DWORD),
                        LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME);
    if (!mapping_handle) abort();

    const auto proc_id_p =
      (DWORD *) MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS,
                              0, 0, sizeof(DWORD));
    if (!proc_id_p) abort();

    *proc_id_p = GetCurrentProcessId();

    auto success_unmap = UnmapViewOfFile(proc_id_p);
    if (!success_unmap) abort();

    auto success_release = ReleaseMutex(mutex_handle);
    if (!success_release) abort();

    // NB: we leave the mutex_handle and mapping_handle open
    return;
  }
  case ERROR_ALREADY_EXISTS: {
    // acquire mutex to shared memory
    auto wait_ret = WaitForSingleObject(mutex_handle, INFINITE);
    if (wait_ret != WAIT_OBJECT_0) abort();

    // read process id from shared memory
    auto mapping_handle =
      OpenFileMapping(FILE_MAP_ALL_ACCESS,
                      FALSE,
                      LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME);
    if (!mapping_handle) abort();

    const auto proc_id_p =
      (DWORD *) MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS,
                              0, 0, sizeof(DWORD));
    if (!proc_id_p) abort();

    const auto proc_id = *proc_id_p;

    auto success_unmap = UnmapViewOfFile(proc_id_p);
    if (!success_unmap) abort();

    auto success_close_mapping = CloseHandle(mapping_handle);
    if (!success_close_mapping) abort();

    auto success_release = ReleaseMutex(mutex_handle);
    if (!success_release) abort();

    auto success_close_mutex = CloseHandle(mutex_handle);
    if (!success_close_mutex) abort();

    if (proc_id != GetCurrentProcessId()) std::exit(on_exit(proc_id));

    return;
  }
  default: {
    log_error("Error on CreateMutex: %s",
              w32util::last_error_message().c_str());
    abort();
  }
  }
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

  auto LOCKBOX_DUPLICATE_INSTANCE_MSG =
    RegisterWindowMessage(LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME);
  if (!LOCKBOX_DUPLICATE_INSTANCE_MSG) {
    throw std::runtime_error("Couldn't get duplicate instance message");
  }

  exit_if_not_single_app_instance([&] (DWORD proc_id) {
      AllowSetForegroundWindow(proc_id);
      PostMessage(HWND_BROADCAST, LOCKBOX_DUPLICATE_INSTANCE_MSG, 0, 0);
      return 0;
    });

  auto TASKBAR_CREATED_MSG =
    RegisterWindowMessage(TASKBAR_CREATED_MESSAGE_NAME);
  if (!TASKBAR_CREATED_MSG) {
    throw std::runtime_error("Couldn't register for TaskbarCreated message");
  }

  auto native_fs = lockbox::create_native_fs();
  auto wd = WindowData {
    /*.native_fs = */std::move(native_fs),
    /*.mounts = */std::vector<MountDetails>(),
    /*.is_stopping = */false,
    /*.TASKBAR_CREATED_MSG = */TASKBAR_CREATED_MSG,
    /*.threads_to_join = */std::vector<ManagedThreadHandle>(),
    /*.LOCKBOX_DUPLICATE_INSTANCE_MSG = */LOCKBOX_DUPLICATE_INSTANCE_MSG,
    /*.last_foreground_window = */NULL,
    /*.popup_menu_is_open = */false,
  };

  // register our main window class
  WNDCLASSEXW wc;
  lockbox::zero_object(wc);
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
  auto hwnd = CreateWindowW(MAIN_WINDOW_CLASS_NAME,
                            L"Lockbox Main Window",
                            WS_OVERLAPPED,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                            /*HWND_MESSAGE*/NULL, NULL, hInstance, &wd);
  if (!hwnd) {
    throw std::runtime_error("Failure to create main window");
  }

  // update window
  UpdateWindow(hwnd);
  ShowWindow(hwnd, SW_HIDE);
  (void) nCmdShow;

  // run message loop
  MSG msg;
  BOOL ret_getmsg;
  while ((ret_getmsg = GetMessageW(&msg, NULL, 0, 0))) {
    if (ret_getmsg == -1) break;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // kill all mounts
  for (const auto & mount : wd.mounts) {
    const auto unmount_success = unmount_drive(NULL, mount);
    if (!unmount_success) {
      log_error("Failed to unmount %s:",
                std::to_string(mount.drive_letter).c_str());
    }
  }

  if (ret_getmsg == -1) throw std::runtime_error("getmessage failed!");

  return msg.wParam;
}

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

#include <encfs/fs/FsIO.h>
#include <encfs/fs/FileUtils.h>

#include <encfs/cipher/MemoryPool.h>

#include <encfs/base/optional.h>

#include <davfuse/log_printer.h>
#include <davfuse/logging.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>

#include <cstdint>

#include <windows.h>
#include <CommCtrl.h>
#include <Shlobj.h>

const wchar_t g_szClassName[] = L"myWindowClass";
const WORD IDC_STATIC = ~0;
const WORD IDPASSWORD = 200;
const WORD IDCONFIRMPASSWORD = 201;
const auto MAX_PASS_LEN = 256;

class WindowData {
public:
  std::shared_ptr<encfs::FsIO> native_fs;
};

class ServerThreadParams {
public:
  DWORD main_thread;
  std::shared_ptr<encfs::FsIO> native_fs;
  encfs::Path encrypted_directory_path;
  encfs::EncfsConfig encfs_config;
  encfs::SecureMem password;
  std::string mount_name;

  ServerThreadParams(DWORD main_thread_,
                     std::shared_ptr<encfs::FsIO> native_fs_,
                     encfs::Path encrypted_directory_path_,
                     encfs::EncfsConfig encfs_config_,
                     encfs::SecureMem password_,
                     std::string mount_name_)
    : main_thread(main_thread_)
    , native_fs(std::move(native_fs_))
    , encrypted_directory_path(std::move(encrypted_directory_path_))
    , encfs_config(std::move(encfs_config_))
    , password(std::move(password_))
    , mount_name(std::move(mount_name_))
  {}
};

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
    memset(&bi, 0, sizeof(bi));
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

  // TODO: actually search for a port to listen on
  port_t listen_port = 8081;
  bool sent_signal = false;

  auto our_callback = [&] (fdevent_loop_t /*loop*/) {
    PostThreadMessage(params->main_thread, MOUNT_DONE_SIGNAL, 1, listen_port);
    sent_signal = true;
  };

  lockbox::run_lockbox_webdav_server(std::move(enc_fs),
                                     std::move(params->encrypted_directory_path),
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
void
mount_encrypted_folder_dialog(HWND owner,
                              std::shared_ptr<encfs::FsIO> native_fs) {
  auto maybe_chosen_folder = get_folder_dialog(owner);
  // they pressed cancel
  if (!maybe_chosen_folder) return;
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
    return;
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
    if (!maybe_encfs_config) return;
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
    return;
  }

  opt::optional<encfs::SecureMem> maybe_password;
  if (maybe_encfs_config) {
    // ask for password
    while (!maybe_password) {
      maybe_password =
        get_password_dialog(owner, encrypted_directory_path);
      if (!maybe_password) return;

      log_debug("verifying password...");
      const auto correct_password =
        w32util::modal_call(owner, "Verifying Password...",
                            "Verifying password...",
                            encfs::verify_password,
                            *maybe_encfs_config, *maybe_password);
      log_debug("verifying done!");
      if (!correct_password) return;

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
    if (!create) return;

    // create new password
    maybe_password =
      get_new_password_dialog(owner, encrypted_directory_path);
    if (!maybe_password) return;

    maybe_encfs_config =
      w32util::modal_call(owner,
                          "Creating New Configuration...",
                          "Creating new configuration...",
                          encfs::create_paranoid_config,
                          *maybe_password);
    if (!maybe_encfs_config) return;

    auto modal_completed =
      w32util::modal_call_void(owner,
                               "Saving New Configuration...",
                               "Saving new configuration...",
                               encfs::write_config,
                               native_fs, encrypted_directory_path,
                               *maybe_encfs_config);
    if (!modal_completed) return;
  }

  // okay now we have:
  // * a valid path
  // * a valid config
  // * a valid password
  // we're ready to mount the drive

  // TODO: configure both /webdav and X:
  auto drive_letter = std::string("X");
  auto mount_name = std::string("webdav");

  auto thread_params =
    new ServerThreadParams(GetCurrentThreadId(),
                           native_fs,
                           std::move(encrypted_directory_path),
                           std::move(*maybe_encfs_config),
                           std::move(*maybe_password),
                           mount_name);

  auto thread_handle =
    CreateThread(NULL, 0, mount_thread, (LPVOID) thread_params, 0, NULL);
  if (!thread_handle) {
    delete thread_params;
    quick_alert(owner,
                "Unable to start encrypted file system!",
                "Error");
    return;
  }
  auto _close_thread = lockbox::create_destroyer(thread_handle, CloseHandle);

  auto msg_ptr = w32util::modal_until_message(owner,
                                              "Mounting Encrypted Container...",
                                              "Mounting encrypted container...",
                                              MOUNT_DONE_SIGNAL);
  if (!msg_ptr) return;

  assert(msg_ptr->message == MOUNT_DONE_SIGNAL);

  if (!msg_ptr->wParam) {
    quick_alert(owner,
                "Unable to start encrypted file system!",
                "Error");
    return;
  }

  auto listen_port = (port_t) msg_ptr->lParam;

  // just mount the file system using the console command
  std::ostringstream os;
  os << "use " << drive_letter <<
    ": http://localhost:" << listen_port << "/" << mount_name;
  auto ret_shell1 = (int) ShellExecuteW(NULL, L"open",
                                        L"c:\\windows\\system32\\net.exe",
                                        w32util::widen(os.str()).c_str(),
                                        NULL, SW_HIDE);
  // abort if it fails
  if (ret_shell1 <= 32) {
    // TODO kill thread
    log_error("ShellExecuteW error: Ret was: %d", ret_shell1);
    quick_alert(owner,
                "Unable to start encrypted file system!",
                "Error");
    return;
  }

  // now open up the file system with explorer
  auto ret_shell2 =
    (int) ShellExecuteW(NULL, L"explore",
                        w32util::widen(drive_letter + ":\\").c_str(),
                        NULL, NULL, SW_SHOW);
  if (ret_shell2 <= 32) {
    // this is not that bad, we were just opening the windows
    log_error("ShellExecuteW error: Ret was: %d", ret_shell2);
  }
}


CALLBACK
LRESULT
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const auto wd = (WindowData *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
  switch(msg){
  case WM_CREATE: {
    auto pParent = ((LPCREATESTRUCT) lParam)->lpCreateParams;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) pParent);
    break;
  }
  case WM_LBUTTONDOWN: {
    mount_encrypted_folder_dialog(hwnd, wd->native_fs);
    break;
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }
  return 0;
}

WINAPI
int
WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
        LPSTR /*lpCmdLine*/, int nCmdShow)
{
  // TODO: catch all exceptions, since this is a top-level

  // TODO: de-initialize
  log_printer_default_init();
  logging_set_global_level(LOG_DEBUG);
  log_debug("Hello world!");

  // TODO: de-initialize
  auto ret_ole = OleInitialize(NULL);
  if (ret_ole != S_OK) throw std::runtime_error("couldn't initialize ole!");

  INITCOMMONCONTROLSEX icex;
  icex.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
  icex.dwSize = sizeof(icex);
  auto success = InitCommonControlsEx(&icex);
  if (success == FALSE) throw std::runtime_error("Couldn't initialize common controls");

  // TODO: require ComCtl32.dll version >= 6.0
  // (we do this in the manifest but it would be nice
  //  to check at runtime too)

  // TODO: de-initialize
  lockbox::global_webdav_init();

  auto native_fs = lockbox::create_native_fs();
  WindowData wd = {
    .native_fs = std::move(native_fs),
  };

  // TODO: create a window for the tray icon
  //Step 1: Registering the Window Class
  WNDCLASSEX wc;
  wc.cbSize        = sizeof(WNDCLASSEX);
  wc.style         = 0;
  wc.lpfnWndProc   = WndProc;
  wc.cbClsExtra    = 0;
  wc.cbWndExtra    = 0;
  wc.hInstance     = hInstance;
  wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wc.lpszMenuName  = NULL;
  wc.lpszClassName = g_szClassName;
  wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

  if (!RegisterClassExW(&wc)) {
    MessageBoxW(NULL, L"Window Registration Failed!", L"Error!",
                MB_ICONEXCLAMATION | MB_OK);
    return 0;
  }

  // Step 2: Creating the Window
  auto hwnd = CreateWindowExW(WS_EX_CLIENTEDGE,
                              g_szClassName,
                              L"The title of my window",
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
                              NULL, NULL, hInstance, &wd);
  if(hwnd == NULL) {
    MessageBoxW(NULL, L"Window Creation Failed!", L"Error!",
                MB_ICONEXCLAMATION | MB_OK);
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  // Step 3: The Message Loop
  MSG Msg;
  while (GetMessageW(&Msg, NULL, 0, 0)) {
    if (Msg.message == MOUNT_OVER_SIGNAL) {
      // TODO: webdav server died, kill mount
    }
    TranslateMessage(&Msg);
    DispatchMessage(&Msg);
  }

  return Msg.wParam;
}

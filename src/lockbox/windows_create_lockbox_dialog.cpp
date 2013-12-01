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

#include <lockbox/windows_create_lockbox_dialog.hpp>

#include <lockbox/product_name.h>
#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_error.hpp>
#include <lockbox/windows_gui_util.hpp>

#include <encfs/fs/FsIO.h>

#include <sstream>

#include <windows.h>

#include <Shlobj.h>

namespace lockbox { namespace win {

#if 0
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
    w32util::quick_alert(owner, os.str(), "Bad Path!");
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
    w32util::quick_alert(owner, os.str(), "Bad Folder");
    return opt::nullopt;
  }

  opt::optional<encfs::SecureMem> maybe_password;
  if (maybe_encfs_config) {
    // ask for password
    while (!maybe_password) {
      maybe_password =
        get_password_dialog(owner, encrypted_directory_path);
      if (!maybe_password) return opt::nullopt;

      lbx_log_debug("verifying password...");
      const auto correct_password =
        w32util::modal_call(owner, "Verifying Password...",
                            "Verifying password...",
                            encfs::verify_password,
                            *maybe_encfs_config, *maybe_password);
      lbx_log_debug("verifying done!");
      if (!correct_password) return opt::nullopt;

      if (!*correct_password) {
        w32util::quick_alert(owner, "Incorrect password! Try again",
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

  auto maybe_mount_details =
    w32util::modal_call(owner,
                        ("Starting New "
                         ENCRYPTED_STORAGE_NAME_A
                         "..."),
                        ("Starting new "
                         ENCRYPTED_STORAGE_NAME_A
                         "..."),
                        lockbox::win::mount_new_encfs_drive,
                        native_fs,
                        encrypted_directory_path,
                        *maybe_encfs_config,
                        *maybe_password);

  if (!maybe_mount_details) return opt::nullopt;

  maybe_mount_details->open_mount();

  bubble_msg(owner,
             "Success",
             "You've successfully started \"" +
             maybe_mount_details->get_mount_name() +
             ".\"");

  return std::move(*maybe_mount_details);
#endif

enum {
  IDC_PASSWORD = 1000,
  IDC_CONFIRM_PASSWORD,
  IDC_LOCATION,
  IDC_BROWSE,
  IDC_NAME,
  IDC_STATIC,
};

static
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
        w32util::quick_alert(owner, os.str(), "Bad Selection!");
      }
    }
    else return opt::nullopt;
  }
}

CALLBACK
static
INT_PTR
create_new_lockbox_dialog_proc(HWND hwnd, UINT Message,
                               WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG: {
    w32util::center_window_in_monitor(hwnd);
    w32util::set_default_dialog_font(hwnd);
    return TRUE;
  }
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDC_BROWSE: {
      auto ret = get_folder_dialog(hwnd);
      if (ret) {
        SetDlgItemTextW(hwnd, IDC_LOCATION, w32util::widen(*ret).c_str());
      }
      break;
    }
    case IDOK: {
      // TODO: check if location is a well-formed path

      // TODO: check if location exists

      // TODO: check validity of name field
      // non-zero length
      // we can join it to the location path

      // TODO: check if full bitvault path already exists

      auto password_hwnd = GetDlgItem(hwnd, IDC_PASSWORD);
      if (!password_hwnd) throw w32util::windows_error();

      auto confirm_password_hwnd = GetDlgItem(hwnd, IDC_CONFIRM_PASSWORD);
      if (!confirm_password_hwnd) throw w32util::windows_error();

      auto secure_pass_1 =
        w32util::securely_read_text_field(password_hwnd, false);
      auto secure_pass_2 =
        w32util::securely_read_text_field(confirm_password_hwnd, false);

      auto num_chars_1 = strlen((char *) secure_pass_1.data());
      auto num_chars_2 = strlen((char *) secure_pass_2.data());

      // check if password is empty
      if (!num_chars_1) {
        w32util::quick_alert(hwnd, "Empty password is not allowed!",
                             "Invalid Password");
        return TRUE;
      }

      // check if passwords match
      if (num_chars_1 != num_chars_2 ||
          memcmp(secure_pass_1.data(), secure_pass_2.data(), num_chars_1)) {
        w32util::quick_alert(hwnd, "The Passwords do not match!",
                             "Passwords don't match");
        return TRUE;
      }

      // TODO: create paranoid config

      // TODO: write config

      // TODO: mount encfs drive

      w32util::clear_text_field(password_hwnd, num_chars_1);
      w32util::clear_text_field(confirm_password_hwnd, num_chars_2);

      EndDialog(hwnd, (INT_PTR) 0);
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

opt::optional<lockbox::win::MountDetails>
create_new_lockbox_dialog(HWND owner, std::shared_ptr<encfs::FsIO> fsio) {
  using namespace w32util;

  typedef unsigned unit_t;

  // TODO: compute this programmatically
  const unit_t FONT_HEIGHT = 8;

  const unit_t VERT_MARGIN = 8;
  const unit_t HORIZ_MARGIN = 8;

  const unit_t TOP_MARGIN = VERT_MARGIN;
  const unit_t BOTTOM_MARGIN = VERT_MARGIN;
  const unit_t LEFT_MARGIN = HORIZ_MARGIN;
  const unit_t RIGHT_MARGIN = HORIZ_MARGIN;

  const unit_t LABEL_WIDTH = 30;
  const unit_t LABEL_HEIGHT = FONT_HEIGHT;
  const unit_t TEXT_ENTRY_WIDTH = 80;
  const unit_t TEXT_ENTRY_HEIGHT = 11;

  const unit_t BOTTOM_HEADING_MARGIN = TOP_MARGIN;
  const unit_t TOP_BTN_MARGIN = BOTTOM_MARGIN;
  const unit_t BUTTON_H_SPACE = 2;
  const unit_t FORM_H_SPACING = 5;
  const unit_t FORM_V_SPACING = 5;
  const unit_t LABEL_TO_ENTRY_V_OFFSET = -2;

  const unit_t DIALOG_WIDTH = (LEFT_MARGIN + LABEL_WIDTH + FORM_H_SPACING +
                               TEXT_ENTRY_WIDTH + RIGHT_MARGIN);

  // heading "Create New Lockbox" is positioned in top-left
  const unit_t HEADING_WIDTH = DIALOG_WIDTH - LEFT_MARGIN - RIGHT_MARGIN;
  const unit_t HEADING_HEIGHT = FONT_HEIGHT;
  const unit_t HEADING_LEFT = LEFT_MARGIN;
  const unit_t HEADING_TOP = TOP_MARGIN;

  // location label is under heading
  const unit_t LOCATION_LABEL_WIDTH = LABEL_WIDTH;
  const unit_t LOCATION_LABEL_HEIGHT = LABEL_HEIGHT;
  const unit_t LOCATION_LABEL_LEFT = LEFT_MARGIN;
  const unit_t LOCATION_LABEL_TOP = (HEADING_TOP + HEADING_HEIGHT +
                                     BOTTOM_HEADING_MARGIN);

  // browse button WIDTH
  const unit_t BROWSE_BTN_WIDTH = 30;
  const unit_t BROWSE_BTN_SPACE = 2;

  // location entry is to the right of the heading
  const unit_t LOCATION_ENTRY_WIDTH = TEXT_ENTRY_WIDTH - BROWSE_BTN_SPACE - BROWSE_BTN_WIDTH;
  const unit_t LOCATION_ENTRY_HEIGHT = TEXT_ENTRY_HEIGHT;
  const unit_t LOCATION_ENTRY_LEFT = (LOCATION_LABEL_LEFT +
                                      LOCATION_LABEL_WIDTH +
                                      FORM_H_SPACING);
  const unit_t LOCATION_ENTRY_TOP = (LOCATION_LABEL_TOP + LABEL_TO_ENTRY_V_OFFSET);

  // browse button cont'd
  const unit_t BROWSE_BTN_HEIGHT = TEXT_ENTRY_HEIGHT;
  const unit_t BROWSE_BTN_LEFT = (LOCATION_ENTRY_LEFT + LOCATION_ENTRY_WIDTH + BROWSE_BTN_SPACE);
  const unit_t BROWSE_BTN_TOP = (LOCATION_LABEL_TOP + LABEL_TO_ENTRY_V_OFFSET);

#define ALIGN_LABEL(__NAME, PRECEDING_LABEL)                      \
  const unit_t __NAME ## _LABEL_WIDTH = LABEL_WIDTH; \
  const unit_t __NAME ## _LABEL_HEIGHT = LABEL_HEIGHT; \
  const unit_t __NAME ## _LABEL_LEFT = LEFT_MARGIN; \
  const unit_t __NAME ## _LABEL_TOP = (PRECEDING_LABEL ## _LABEL_TOP + \
                                       PRECEDING_LABEL ## _LABEL_HEIGHT + \
                                       FORM_V_SPACING)

#define ALIGN_TEXT_ENTRY(__NAME) \
  const unit_t __NAME ## _ENTRY_WIDTH = TEXT_ENTRY_WIDTH; \
  const unit_t __NAME ## _ENTRY_HEIGHT = TEXT_ENTRY_HEIGHT; \
  const unit_t __NAME ## _ENTRY_LEFT = (__NAME ## _LABEL_LEFT + \
                                        __NAME ## _LABEL_WIDTH + FORM_H_SPACING); \
  const unit_t __NAME ## _ENTRY_TOP = __NAME ## _LABEL_TOP + LABEL_TO_ENTRY_V_OFFSET

  ALIGN_LABEL(NAME, LOCATION);
  ALIGN_TEXT_ENTRY(NAME);

  ALIGN_LABEL(PASS, NAME);
  ALIGN_TEXT_ENTRY(PASS);

  ALIGN_LABEL(CONFIRM, PASS);
  ALIGN_TEXT_ENTRY(CONFIRM);

  // cancel btn
  const unit_t CANCEL_BTN_WIDTH = 40;
  const unit_t CANCEL_BTN_HEIGHT = 11;
  const unit_t CANCEL_BTN_LEFT = DIALOG_WIDTH - RIGHT_MARGIN - CANCEL_BTN_WIDTH;
  const unit_t CANCEL_BTN_TOP = (CONFIRM_ENTRY_TOP + CONFIRM_ENTRY_HEIGHT +
                                 TOP_BTN_MARGIN);

  // ok btn
  const unit_t OK_BTN_WIDTH = 37;
  const unit_t OK_BTN_HEIGHT = 11;
  const unit_t OK_BTN_LEFT = (CANCEL_BTN_LEFT - BUTTON_H_SPACE - OK_BTN_WIDTH);
  const unit_t OK_BTN_TOP = (CONFIRM_ENTRY_TOP + CONFIRM_ENTRY_HEIGHT +
                             TOP_BTN_MARGIN);

  const unit_t DIALOG_HEIGHT = OK_BTN_TOP + OK_BTN_HEIGHT + BOTTOM_MARGIN;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Create New " ENCRYPTED_STORAGE_NAME_A,
                              0, 0, DIALOG_WIDTH, DIALOG_HEIGHT),
                   {
                     LText(("Create New " ENCRYPTED_STORAGE_NAME_A),
                           IDC_STATIC,
                           HEADING_LEFT, HEADING_TOP,
                           HEADING_WIDTH, HEADING_HEIGHT),
                     LText("Location:", IDC_STATIC,
                           LOCATION_LABEL_LEFT, LOCATION_LABEL_TOP,
                           LOCATION_LABEL_WIDTH, LOCATION_LABEL_HEIGHT),
                     EditText(IDC_LOCATION,
                              LOCATION_ENTRY_LEFT, LOCATION_ENTRY_TOP,
                              LOCATION_ENTRY_WIDTH, LOCATION_ENTRY_HEIGHT,
                              ES_READONLY | ES_LEFT |
                              WS_BORDER),
                     PushButton("Browse", IDC_BROWSE,
                                BROWSE_BTN_LEFT, BROWSE_BTN_TOP,
                                BROWSE_BTN_WIDTH, BROWSE_BTN_HEIGHT),
                     LText("Name:", IDC_STATIC,
                           NAME_LABEL_LEFT, NAME_LABEL_TOP,
                           NAME_LABEL_WIDTH, NAME_LABEL_HEIGHT),
                     EditText(IDC_NAME,
                              NAME_ENTRY_LEFT, NAME_ENTRY_TOP,
                              NAME_ENTRY_WIDTH, NAME_ENTRY_HEIGHT),
                     LText("Password:", IDC_STATIC,
                           PASS_LABEL_LEFT, PASS_LABEL_TOP,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDC_PASSWORD,
                              PASS_ENTRY_LEFT, PASS_ENTRY_TOP,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     LText("Confirm:", IDC_STATIC,
                           CONFIRM_LABEL_LEFT, CONFIRM_LABEL_TOP,
                           CONFIRM_LABEL_WIDTH, CONFIRM_LABEL_HEIGHT),
                     EditText(IDC_CONFIRM_PASSWORD,
                              CONFIRM_ENTRY_LEFT, CONFIRM_ENTRY_TOP,
                              CONFIRM_ENTRY_WIDTH, CONFIRM_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     DefPushButton("OK", IDOK,
                                   OK_BTN_LEFT, OK_BTN_TOP,
                                   OK_BTN_WIDTH, OK_BTN_HEIGHT),
                     PushButton("Cancel", IDCANCEL,
                                CANCEL_BTN_LEFT, CANCEL_BTN_TOP,
                                CANCEL_BTN_WIDTH, CANCEL_BTN_HEIGHT),
                   }
                   );

  (void) fsio;
  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      owner, create_new_lockbox_dialog_proc);
  if (!ret_ptr) return opt::nullopt;

  return opt::nullopt;
}

}}

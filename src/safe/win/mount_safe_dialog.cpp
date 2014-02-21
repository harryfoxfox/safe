/*
  Safe: Encrypted File System
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

#include <safe/win/mount_safe_dialog.hpp>

#include <safe/constants.h>
#include <safe/win/dialog_common.hpp>
#include <safe/mount_safe_dialog_logic.hpp>
#include <safe/win/mount.hpp>
#include <safe/util.hpp>
#include <safe/win/report_bug_dialog.hpp>
#include <safe/report_exception.hpp>

#include <w32util/async.hpp>
#include <w32util/dialog.hpp>
#include <w32util/gui_util.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/base/optional.h>

#include <windows.h>

namespace safe { namespace win {

enum {
  IDC_LOCATION = 1000,
  IDC_BROWSE,
  IDC_PASSWORD,
  IDC_STATIC,
};

struct MountExistingSafeDialogCtx {
  std::shared_ptr<encfs::FsIO> fs;
  safe::win::TakeMountFn take_mount;
  safe::win::HaveMountFn have_mount;
  opt::optional<encfs::Path> initial_path;
};

CALLBACK
static
INT_PTR
mount_existing_safe_dialog_proc(HWND hwnd, UINT Message,
                                   WPARAM wParam, LPARAM lParam) {
  const auto ctx = (MountExistingSafeDialogCtx *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (Message) {
  case WM_INITDIALOG: {
    const auto ctx = (MountExistingSafeDialogCtx *) lParam;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) ctx);
    w32util::center_window_in_monitor(hwnd);
    w32util::set_default_dialog_font(hwnd);

    if (ctx->initial_path) {
      SetDlgItemTextW(hwnd, IDC_LOCATION, w32util::widen(*ctx->initial_path).c_str());
      SetFocus(GetDlgItem(hwnd, IDC_PASSWORD));
      return FALSE;
    }

    return TRUE;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDC_BROWSE: {
      auto ret = w32util::get_folder_dialog(hwnd);
      if (ret) {
        SetDlgItemTextW(hwnd, IDC_LOCATION, w32util::widen(*ret).c_str());
      }
      break;
    }
    case IDOK: {
      auto location_hwnd = GetDlgItem(hwnd, IDC_LOCATION);
      if (!location_hwnd) w32util::throw_windows_error();
      auto password_hwnd = GetDlgItem(hwnd, IDC_PASSWORD);
      if (!password_hwnd) w32util::throw_windows_error();

      auto location_string = w32util::read_text_field(location_hwnd);
      auto password_buf =
        w32util::securely_read_text_field(password_hwnd, false);

      auto error_msg =
        safe::verify_mount_safe_dialog_fields(ctx->fs, location_string,
                                                    password_buf);
      if (error_msg) {
        w32util::quick_alert(hwnd, error_msg->message, error_msg->title);
        break;
      }

      opt::optional<encfs::EncfsConfig> maybe_cfg;
      try {
        auto encrypted_container_path = ctx->fs->pathFromString(location_string);

        maybe_cfg =
          w32util::modal_call(hwnd,
                              SAFE_PROGRESS_READING_CONFIG_TITLE,
                              SAFE_PROGRESS_READING_CONFIG_MESSAGE,
                              encfs::read_config, ctx->fs, encrypted_container_path);
        if (!maybe_cfg) {
          // modal_call returns nullopt if we got a quit signal
          EndDialog(hwnd, (INT_PTR) 0);
          break;
        }

        auto cfg = std::move(*maybe_cfg);

        opt::optional<safe::win::MountDetails> maybe_mount_details;
        if (ctx->have_mount(encrypted_container_path)) {
          auto maybe_pass_is_correct =
            w32util::modal_call(hwnd,
                                SAFE_PROGRESS_VERIFYING_PASS_TITLE,
                                SAFE_PROGRESS_VERIFYING_PASS_MESSAGE,
                                encfs::verify_password, cfg, password_buf);
          if (!maybe_pass_is_correct) {
            EndDialog(hwnd, (INT_PTR) 0);
            break;
          }

          auto pass_is_correct = *maybe_pass_is_correct;
          if (!pass_is_correct) throw encfs::BadPassword();

          // check if path is already mounted and return that instead
          maybe_mount_details = ctx->take_mount(encrypted_container_path);
          assert(maybe_mount_details);
        }
        else {
          maybe_mount_details = w32util::modal_call(hwnd,
                                                    SAFE_PROGRESS_MOUNTING_EXISTING_TITLE,
                                                    SAFE_PROGRESS_MOUNTING_EXISTING_MESSAGE,
                                                    safe::win::mount_new_encfs_drive,
                                                    ctx->fs, encrypted_container_path, cfg, password_buf);
        }

        // modal_call returns nullopt if we got a quit signal
        if (!maybe_mount_details) {
          EndDialog(hwnd, (INT_PTR) 0);
          break;
        }

        w32util::clear_text_field(password_hwnd,
                                  strlen((char *) password_buf.data()));

        EndDialog(hwnd, send_mount_details(std::move(maybe_mount_details)));
      }
      catch (const encfs::ConfigurationFileDoesNotExist &) {
        w32util::quick_alert(hwnd,
                             SAFE_DIALOG_NO_CONFIG_EXISTS_TITLE,
                             SAFE_DIALOG_NO_CONFIG_EXISTS_MESSAGE);
      }
      catch (const encfs::BadPassword &) {
        w32util::quick_alert(hwnd,
                             SAFE_DIALOG_PASS_INCORRECT_TITLE,
                             SAFE_DIALOG_PASS_INCORRECT_MESSAGE);
      }
      catch (const std::exception &) {
        auto choice =
          safe::win::report_bug_dialog(hwnd,
                                       "An error occured while attempting to mount \"" +
                                       location_string + "\"");

        if (choice == safe::win::ReportBugDialogChoice::REPORT_BUG) {
          safe::report_exception(safe::ExceptionLocation::MOUNT,
                                 std::current_exception());
        }

        EndDialog(hwnd, (INT_PTR) 0);
      }

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

opt::optional<safe::win::MountDetails>
mount_existing_safe_dialog(HWND hwnd, std::shared_ptr<encfs::FsIO> fsio,
                           safe::win::TakeMountFn take_mount,
                           safe::win::HaveMountFn have_mount,
                           opt::optional<encfs::Path> initial_path) {
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

  // heading "Mount Existing Safe" is positioned in top-left
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

  ALIGN_LABEL(PASS, LOCATION);
  ALIGN_TEXT_ENTRY(PASS);

  // cancel btn
  const unit_t CANCEL_BTN_WIDTH = 40;
  const unit_t CANCEL_BTN_HEIGHT = 11;
  const unit_t CANCEL_BTN_LEFT = DIALOG_WIDTH - RIGHT_MARGIN - CANCEL_BTN_WIDTH;
  const unit_t CANCEL_BTN_TOP = (PASS_ENTRY_TOP + PASS_ENTRY_HEIGHT +
                                 TOP_BTN_MARGIN);

  // ok btn
  const unit_t OK_BTN_WIDTH = 37;
  const unit_t OK_BTN_HEIGHT = 11;
  const unit_t OK_BTN_LEFT = (CANCEL_BTN_LEFT - BUTTON_H_SPACE - OK_BTN_WIDTH);
  const unit_t OK_BTN_TOP = CANCEL_BTN_TOP;

  const unit_t DIALOG_HEIGHT = OK_BTN_TOP + OK_BTN_HEIGHT + BOTTOM_MARGIN;

  auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Mount Existing " ENCRYPTED_STORAGE_NAME_A,
                              0, 0, DIALOG_WIDTH, DIALOG_HEIGHT),
                   {
                     LText(("Mount Existing " ENCRYPTED_STORAGE_NAME_A),
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
                     LText("Password:", IDC_STATIC,
                           PASS_LABEL_LEFT, PASS_LABEL_TOP,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDC_PASSWORD,
                              PASS_ENTRY_LEFT, PASS_ENTRY_TOP,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
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

  MountExistingSafeDialogCtx ctx = {
    std::move(fsio), std::move(take_mount), std::move(have_mount), std::move(initial_path),
  };
  auto ret_ptr =
    DialogBoxIndirectParam(GetModuleHandle(NULL),
                           dlg.get_data(),
                           hwnd, mount_existing_safe_dialog_proc,
                           (LPARAM) &ctx);

  return receive_mount_details(ret_ptr);
}

}}

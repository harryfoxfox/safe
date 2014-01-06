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

#include <lockbox/welcome_dialog_win.hpp>

#include <lockbox/constants.h>
#include <lockbox/dialog_common_win.hpp>
#include <lockbox/logging.h>
#include <lockbox/resources_win.h>
#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_gui_util.hpp>

#include <windows.h>

namespace lockbox { namespace win {

enum {
  IDC_LOGO = 1000,
  IDC_CREATE,
  IDC_MOUNT,
  IDC_STATIC,
};

void
draw_icon_item(LPDRAWITEMSTRUCT pDIS, LPCWSTR icon_resource) {
  auto width = pDIS->rcItem.right - pDIS->rcItem.left;
  auto height = pDIS->rcItem.bottom - pDIS->rcItem.top;

  auto icon_handle = (HICON) LoadImage(GetModuleHandle(NULL),
                                       icon_resource, IMAGE_ICON,
                                       width, height,
                                       0);
  if (!icon_handle) throw w32util::windows_error();

  auto _release_icon =
    lockbox::create_deferred(DestroyIcon, icon_handle);
  auto success = DrawIconEx(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top,
                            icon_handle, width, height,
                            0, NULL, DI_NORMAL);
  if (!success) throw w32util::windows_error();
}

CALLBACK
static
INT_PTR
welcome_dialog_proc(HWND hwnd, UINT Message,
                    WPARAM wParam, LPARAM lParam) {
  (void) lParam;

  switch (Message) {
  case WM_INITDIALOG: {
    w32util::center_window_in_monitor(hwnd);
    w32util::set_default_dialog_font(hwnd);
    return TRUE;
  }
  case WM_DRAWITEM: {
    auto pDIS = (LPDRAWITEMSTRUCT) lParam;
    if (pDIS->CtlID == IDC_LOGO) draw_icon_item(pDIS, IDI_LBX_APP);
    return TRUE;
  }
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDC_CREATE: {
      EndDialog(hwnd, send_dialog_box_data(WelcomeDialogChoice::CREATE_NEW_LOCKBOX));
      break;
    }
    case IDC_MOUNT: {
      EndDialog(hwnd, send_dialog_box_data(WelcomeDialogChoice::MOUNT_EXISTING_LOCKBOX));
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, send_dialog_box_data(WelcomeDialogChoice::NOTHING));
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

WelcomeDialogChoice
welcome_dialog(HWND hwnd) {
  using namespace w32util;

  typedef unsigned unit_t;

  const unit_t TOP_MARGIN = 8;
  const unit_t BOTTOM_MARGIN = 8;
  const unit_t LEFT_MARGIN = 8;
  const unit_t RIGHT_MARGIN = 8;

  const unit_t ICON_WIDTH = 64;
  const unit_t ICON_HEIGHT = 64;
  const unit_t ICON_TOP = TOP_MARGIN;
  const unit_t ICON_LEFT = LEFT_MARGIN;

  const unit_t TEXT_WIDTH = 64 * 2;
  const unit_t TEXT_HEIGHT = 64;
  const unit_t TEXT_LEFT = ICON_LEFT + ICON_WIDTH + LEFT_MARGIN;
  const unit_t TEXT_TOP = TOP_MARGIN;

  const unit_t DIALOG_WIDTH =
    LEFT_MARGIN + ICON_WIDTH + LEFT_MARGIN + TEXT_WIDTH + RIGHT_MARGIN;
  const unit_t DIALOG_HEIGHT = TOP_MARGIN + ICON_HEIGHT + BOTTOM_MARGIN;

  // place mount button first, since the buttons hug the right
  const unit_t MOUNT_BTN_WIDTH = 64;
  const unit_t MOUNT_BTN_HEIGHT = 11;
  const unit_t MOUNT_BTN_LEFT = DIALOG_WIDTH - RIGHT_MARGIN - MOUNT_BTN_WIDTH;
  const unit_t MOUNT_BTN_TOP = DIALOG_HEIGHT - BOTTOM_MARGIN - MOUNT_BTN_HEIGHT;

  const unit_t CREATE_BTN_WIDTH = 57;
  const unit_t CREATE_BTN_HEIGHT = 11;
  const unit_t CREATE_BTN_LEFT = MOUNT_BTN_LEFT - 4 - CREATE_BTN_WIDTH;
  const unit_t CREATE_BTN_TOP = DIALOG_HEIGHT - BOTTOM_MARGIN - MOUNT_BTN_HEIGHT;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Welcome to Safe!", 0, 0,
                              DIALOG_WIDTH, DIALOG_HEIGHT),
                   {
                     LText(LOCKBOX_DIALOG_WELCOME_TEXT, IDC_STATIC,
                           TEXT_LEFT, TEXT_TOP,
                           TEXT_WIDTH, TEXT_HEIGHT),
                     Control("", IDC_LOGO, ControlClass::STATIC,
                             SS_OWNERDRAW, ICON_LEFT, ICON_TOP,
                             ICON_WIDTH, ICON_HEIGHT),
                     DefPushButton(LOCKBOX_DIALOG_WELCOME_CREATE_BUTTON, IDC_CREATE,
                                   CREATE_BTN_LEFT, CREATE_BTN_TOP,
                                   CREATE_BTN_WIDTH, CREATE_BTN_HEIGHT),
                     PushButton(LOCKBOX_DIALOG_WELCOME_MOUNT_BUTTON, IDC_MOUNT,
                                MOUNT_BTN_LEFT, MOUNT_BTN_TOP,
                                MOUNT_BTN_WIDTH, MOUNT_BTN_HEIGHT),
                   }
                   );

  auto ret = DialogBoxIndirect(GetModuleHandle(NULL),
                               dlg.get_data(),
                               hwnd, welcome_dialog_proc);
  if (ret == 0 || ret == (INT_PTR) -1) throw windows_error();

  return receive_dialog_box_data<WelcomeDialogChoice>(ret);
}

}}

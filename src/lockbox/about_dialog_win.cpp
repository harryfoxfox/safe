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

#include <lockbox/about_dialog_win.hpp>

#include <lockbox/constants.h>
#include <lockbox/logging.h>
#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_gui_util.hpp>
#include <lockbox/windows_string.hpp>

#include <cassert>

#include <windows.h>
#include <shellapi.h>

namespace lockbox { namespace win {

enum {
  IDC_BLURB = 1000,
  IDC_GET_SOURCE_CODE,
};

static
bool
open_src_code(HWND owner) {
  auto ret_shell2 =
    (int) ShellExecuteW(owner, L"open",
                        w32util::widen(LOCKBOX_SOURCE_CODE_WEBSITE).c_str(),
                        NULL, NULL, SW_SHOWNORMAL);
  return ret_shell2 > 32;
}

CALLBACK
static
INT_PTR
about_dialog_proc(HWND hwnd, UINT Message,
                  WPARAM wParam, LPARAM /*lParam*/) {

  switch (Message) {
  case WM_INITDIALOG: {
    w32util::set_default_dialog_font(hwnd);

    // position everything
    typedef unsigned unit_t;

    // compute size of about string
    auto text_hwnd = GetDlgItem(hwnd, IDC_BLURB);
    if (!text_hwnd) throw w32util::windows_error();

    const unit_t BLURB_TEXT_WIDTH = 300;

    const unit_t BUTTON_WIDTH_CREATELB_DLG = 35;
    const unit_t BUTTON_WIDTH_GET_SOURCE_CODELB_DLG = 50;
    const unit_t BUTTON_HEIGHT_DLG = 14;
    const unit_t BUTTON_H_SPACING_DLG = 2;

    RECT r;
    lockbox::zero_object(r);
    r.left = BUTTON_WIDTH_GET_SOURCE_CODELB_DLG;
    r.right = BUTTON_WIDTH_CREATELB_DLG;
    r.top = BUTTON_HEIGHT_DLG;
    auto success_map = MapDialogRect(hwnd, &r);
    if (!success_map) throw w32util::windows_error();

    const unit_t BUTTON_WIDTH_GET_SOURCE_CODELB = r.left;
    const unit_t BUTTON_WIDTH_CREATELB = r.right;
    const unit_t BUTTON_HEIGHT = r.top;

    lockbox::zero_object(r);
    r.left = BUTTON_H_SPACING_DLG;
    auto success_map_2 = MapDialogRect(hwnd, &r);
    if (!success_map_2) throw w32util::windows_error();

    const unit_t BUTTON_H_SPACING = r.left;

    auto blurb_text = w32util::widen(LOCKBOX_ABOUT_BLURB);

    // get necessary height to contain the text at the desired
    // `BLURB_TEXT_WIDTH` via DrawText()
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
    auto set_client_area_1 = w32util::SetClientSizeInLogical(text_hwnd, true,
                                                             margin, margin,
                                                             w, h);
    if (!set_client_area_1) throw w32util::windows_error();

    // align "Get Source Code" button
    auto get_source_code_hwnd = GetDlgItem(hwnd, IDC_GET_SOURCE_CODE);
    if (!get_source_code_hwnd) throw w32util::windows_error();

    w32util::SetClientSizeInLogical(get_source_code_hwnd, true,
                                    DIALOG_WIDTH -
                                    margin - BUTTON_WIDTH_CREATELB -
                                    BUTTON_H_SPACING - BUTTON_WIDTH_GET_SOURCE_CODELB,
                                    margin + h + margin,
                                    BUTTON_WIDTH_GET_SOURCE_CODELB, BUTTON_HEIGHT);

    // align "OK" button
    auto ok_hwnd = GetDlgItem(hwnd, IDOK);
    if (!ok_hwnd) throw w32util::windows_error();

    w32util::SetClientSizeInLogical(ok_hwnd, true,
                                    DIALOG_WIDTH -
                                    margin - BUTTON_WIDTH_CREATELB,
                                    margin + h + margin,
                                    BUTTON_WIDTH_CREATELB, BUTTON_HEIGHT);

    // set focus on "Ok" button
    PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM) ok_hwnd, TRUE);

    // set up about window (size + position)
    auto set_client_area_2 = w32util::SetClientSizeInLogical(hwnd, true, 0, 0,
                                                             DIALOG_WIDTH,
                                                             DIALOG_HEIGHT);
    if (!set_client_area_2) throw w32util::windows_error();

    w32util::center_window_in_monitor(hwnd);

    return TRUE;
  }

  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDC_GET_SOURCE_CODE: {
      auto success = open_src_code(hwnd);
      if (!success) lbx_log_error("Error opening source website");
      return TRUE;
    }
    case IDOK: case IDCANCEL: {
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

void
about_dialog(HWND hwnd, std::string title) {
  using namespace w32util;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              title, 0, 0, 500, 500),
                   {
                     LText("", IDC_BLURB,
                           0, 0, 0, 0),
                     PushButton("Get Source Code", IDC_GET_SOURCE_CODE,
                                0, 0, 0, 0),
                     DefPushButton("OK", IDOK,
                                   0, 0, 0, 0),
                   }
                   );

  DialogBoxIndirect(GetModuleHandle(NULL),
                    dlg.get_data(),
                    hwnd, about_dialog_proc);
}

}}

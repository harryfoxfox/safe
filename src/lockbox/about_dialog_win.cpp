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
#include <lockbox/dialog_common_win.hpp>
#include <lockbox/logging.h>
#include <lockbox/resources_win.h>
#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_gui_util.hpp>
#include <lockbox/windows_string.hpp>

#include <cassert>

#include <windows.h>
#include <shellapi.h>

namespace lockbox { namespace win {

enum {
  IDC_LOGO = 100,
  IDC_LOGO_TEXT,
  IDC_TAGLINE,
  IDC_VERSION,
  IDC_GET_SOURCE_CODE,
  IDC_BYLINE,
};

enum {
  LOGO_TEXT_HEIGHT_DIALOG_UNITS=36,
};

struct DeleteObjectDestroyer {
  void operator()(HGDIOBJ a) {
    auto ret = DeleteObject(a);
    if (!ret) throw std::runtime_error("couldn't free!");
  }
};

typedef lockbox::ManagedResource<HFONT, DeleteObjectDestroyer> ManagedFontHandle;

struct AboutDialogCtx {
  ManagedFontHandle logo_text_font;
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
                  WPARAM wParam, LPARAM lParam) {
  const auto ctx = (AboutDialogCtx *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
  (void) ctx;

  switch (Message) {
  case WM_INITDIALOG: {
    const auto ctx = (AboutDialogCtx *) lParam;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) ctx);

    w32util::center_window_in_monitor(hwnd);

    RECT r = {0, 0, 0, LOGO_TEXT_HEIGHT_DIALOG_UNITS};
    auto success = MapDialogRect(hwnd, &r);
    if (!success) throw w32util::windows_error();

    // font for logo text
    auto font = CreateFontW(-r.bottom, 0,
                            0, 0, FW_MEDIUM,
                            FALSE, FALSE,
                            FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                            CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                            DEFAULT_PITCH, L"Georgia");
    if (!font) throw w32util::windows_error();
    ctx->logo_text_font.reset(font);

    SendDlgItemMessage(hwnd, IDC_LOGO_TEXT, WM_SETFONT,
                       (WPARAM) font, (LPARAM) 0);

    return TRUE;
  }
  case WM_DRAWITEM: {
    auto pDIS = (LPDRAWITEMSTRUCT) lParam;
    if (pDIS->CtlID == IDC_LOGO) draw_icon_item(pDIS, IDI_LBX_APP);
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
    // we don't actually handle this
    return FALSE;
  }

  default: return FALSE;
  }

  // not reached
  assert(false);
}

LONG
compute_point_size_of_logfont(HWND hwnd, const LOGFONT *lplf) {
  assert(lplf->lfHeight < 0);
  auto hDC = GetDC(hwnd);
  assert(GetMapMode(hDC) == MM_TEXT);
  if (!hDC) throw w32util::windows_error();
  auto _release_dc_1 =
    lockbox::create_deferred(ReleaseDC, hwnd, hDC);
  return MulDiv(-lplf->lfHeight, 72,
                GetDeviceCaps(hDC, LOGPIXELSY));
}

SIZE
compute_base_units_of_logfont(HWND hwnd, const LOGFONT *lplf) {
  // LOGFONT already gives us the height of the font
  // we just need to compute the average width
  auto handle_font = CreateFontIndirect(lplf);
  if (!handle_font) throw w32util::windows_error();
  auto _free_handle_font =
    lockbox::create_deferred(DeleteObject, handle_font);

  auto hDC = GetDC(hwnd);
  if (!hDC) throw w32util::windows_error();
  auto _free_hDC = lockbox::create_deferred(ReleaseDC, hwnd, hDC);

  SIZE out;
  auto old_object = SelectObject(hDC, handle_font);
  if (!old_object) throw w32util::windows_error();
  auto _restore_object = lockbox::create_deferred(SelectObject, hDC, old_object);

  TEXTMETRIC tm;
  auto success_1 = GetTextMetrics(hDC, &tm);
  if (!success_1) throw w32util::windows_error();

  auto success =
    GetTextExtentPoint32W(hDC,
                          L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                          52, &out);
  if (!success) throw w32util::windows_error();

  auto baseunit_x = (out.cx / 26 + 1) / 2;
  auto baseunit_y = tm.tmHeight;

  return {baseunit_x, baseunit_y};
}

void
about_dialog(HWND hwnd) {
  using namespace w32util;

  LOGFONT message_font;
  get_message_font(&message_font);

  auto desired_point = compute_point_size_of_logfont(hwnd, &message_font);
  auto base_units = compute_base_units_of_logfont(hwnd, &message_font);

  lbx_log_error("base units: 0x%x, %ld %ld, point_size: %d",
                (unsigned) GetDialogBaseUnits(),
                (long) base_units.cx, (long) base_units.cy,
                (int) desired_point);
  typedef unsigned unit_t;

  const unit_t FONT_HEIGHT = 8;
  const unit_t TOP_MARGIN = 8;
  const unit_t BOTTOM_MARGIN = 8;

  const unit_t DIALOG_WIDTH = 154;

  const unit_t LOGO_WIDTH = MulDiv(128, 4, base_units.cx);
  const unit_t LOGO_HEIGHT = MulDiv(128, 8, base_units.cy);
  const unit_t LOGO_LEFT = center_offset(DIALOG_WIDTH, LOGO_WIDTH);
  const unit_t LOGO_TOP = TOP_MARGIN;

  const unit_t LOGO_TEXT_WIDTH = DIALOG_WIDTH;
  const unit_t LOGO_TEXT_HEIGHT = LOGO_TEXT_HEIGHT_DIALOG_UNITS;
  const unit_t LOGO_TEXT_LEFT = 0;
  const unit_t LOGO_TEXT_TOP = LOGO_TOP + LOGO_HEIGHT + 4;

  const unit_t TAGLINE_WIDTH = DIALOG_WIDTH;
  const unit_t TAGLINE_HEIGHT = 8;
  const unit_t TAGLINE_LEFT = 0;
  const unit_t TAGLINE_TOP = LOGO_TEXT_TOP + LOGO_TEXT_HEIGHT + 8;

  const unit_t VERSION_WIDTH = DIALOG_WIDTH;
  const unit_t VERSION_HEIGHT = FONT_HEIGHT;
  const unit_t VERSION_LEFT = 0;
  const unit_t VERSION_TOP = TAGLINE_TOP + TAGLINE_HEIGHT + 8;

  const unit_t SOURCE_WIDTH = 73;
  const unit_t SOURCE_HEIGHT = 12;
  const unit_t SOURCE_LEFT = center_offset(DIALOG_WIDTH, SOURCE_WIDTH);
  const unit_t SOURCE_TOP = VERSION_TOP + VERSION_HEIGHT + 8;

  const unit_t BYLINE_WIDTH = DIALOG_WIDTH;
  const unit_t BYLINE_HEIGHT = 8;
  const unit_t BYLINE_LEFT = 0;
  const unit_t BYLINE_TOP = SOURCE_TOP + SOURCE_HEIGHT + 8;

  const unit_t DIALOG_HEIGHT = BYLINE_TOP + BYLINE_HEIGHT + BOTTOM_MARGIN;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "About Safe", 0, 0, DIALOG_WIDTH, DIALOG_HEIGHT,
                              FontDescription(desired_point, narrow(message_font.lfFaceName))),
                   {
                     CText(PRODUCT_NAME_A, IDC_LOGO_TEXT,
                           LOGO_TEXT_LEFT, LOGO_TEXT_TOP,
                           LOGO_TEXT_WIDTH, LOGO_TEXT_HEIGHT),
                     Control("", IDC_LOGO, ControlClass::STATIC,
                             SS_OWNERDRAW, LOGO_LEFT, LOGO_TOP,
                             LOGO_WIDTH, LOGO_HEIGHT),
                     CText(LOCKBOX_DIALOG_ABOUT_TAGLINE, IDC_TAGLINE,
                           TAGLINE_LEFT, TAGLINE_TOP,
                           TAGLINE_WIDTH, TAGLINE_HEIGHT),
                     CText(LOCKBOX_DIALOG_ABOUT_VERSION, IDC_VERSION,
                           VERSION_LEFT, VERSION_TOP,
                           VERSION_WIDTH, VERSION_HEIGHT),
                     PushButton("Get Source Code...", IDC_GET_SOURCE_CODE,
                                SOURCE_LEFT, SOURCE_TOP,
                                SOURCE_WIDTH, SOURCE_HEIGHT),
                     CText(LOCKBOX_DIALOG_ABOUT_BYLINE, IDC_BYLINE,
                           BYLINE_LEFT, BYLINE_TOP,
                           BYLINE_WIDTH, BYLINE_HEIGHT),
                   }
                   );

  AboutDialogCtx ctx;
  DialogBoxIndirectParam(GetModuleHandle(NULL),
                         dlg.get_data(),
                         hwnd, about_dialog_proc, (LPARAM) &ctx);
}

}}

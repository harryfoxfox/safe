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

#include <safe/win/general_safe_dialog.hpp>

#include <safe/constants.h>
#include <safe/win/dialog_common.hpp>
#include <safe/logging.h>
#include <safe/resources_win.h>
#include <w32util/dialog.hpp>
#include <w32util/gui_util.hpp>
#include <safe/util.hpp>

#include <windows.h>

namespace safe { namespace win {

namespace _int {

enum {
  IDC_LOGO = 1000,
  IDC_STATIC,
  IDC_BUTTON_BASE,
};

struct GeneralSafeDialogCtx {
  generic_choices_type choices;
  opt::optional<ButtonAction<generic_choice_type>> close_action;
  LPCWSTR icon_name;
  bool is_system;
};

CALLBACK
static
INT_PTR
general_safe_dialog_proc(HWND hwnd, UINT Message,
                         WPARAM wParam, LPARAM lParam) {
  const auto ctx = (GeneralSafeDialogCtx *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (Message) {
  case WM_INITDIALOG: {
    const auto new_ctx = (decltype(ctx)) lParam;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) new_ctx);
    w32util::center_window_in_monitor(hwnd);

    // disable close button if there is no close action
    if (!new_ctx->close_action) {
      EnableMenuItem(GetSystemMenu(hwnd, FALSE), SC_CLOSE,
                     MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
    }

    return TRUE;
  }
  case WM_DRAWITEM: {
    auto pDIS = (LPDRAWITEMSTRUCT) lParam;
    if (pDIS->CtlID == IDC_LOGO) {
      draw_icon_item(pDIS, ctx->icon_name, ctx->is_system);
    }
    return TRUE;
  }
  case WM_COMMAND: {
    auto command = LOWORD(wParam);
    opt::optional<generic_choice_type> choice_number;
    if (command == IDCANCEL) {
      if (ctx->close_action) {
        choice_number = (*ctx->close_action)();
      }
    }
    else {
      assert(command >= IDC_BUTTON_BASE);
      const auto & choice = ctx->choices[command - IDC_BUTTON_BASE];
      choice_number = choice.fn();
    }

    if (choice_number) {
      EndDialog(hwnd, send_dialog_box_data(*choice_number));
    }
    break;
  }
  default:
    return FALSE;
  }
  return TRUE;
}

_int::generic_choice_type
generic_general_safe_dialog(HWND hwnd,
                            std::string title,
                            std::string msg,
                            _int::generic_choices_type choices,
                            opt::optional<ButtonAction<generic_choice_type>> close_action,
                            GeneralDialogIcon t) {
  using namespace w32util;

  auto message_font = get_message_font();
  auto desired_point = compute_point_size_of_logfont(hwnd, message_font);
  auto base_units = compute_base_units_of_logfont(hwnd, message_font);

  typedef unsigned unit_t;

  const unit_t FONT_HEIGHT = 8;

  // the margin is the font height
  const unit_t TOP_MARGIN = FONT_HEIGHT;
  const unit_t BOTTOM_MARGIN = FONT_HEIGHT;
  const unit_t LEFT_MARGIN = MulDiv(base_units.cy, 4, base_units.cx);
  const unit_t RIGHT_MARGIN = LEFT_MARGIN;

  // the safe icon is 128px
  size_t icon_width_px = 128;
  size_t icon_height_px = 128;
  auto icon_margin = LEFT_MARGIN;
  auto icon_tag = IDI_SFX_APP;
  auto is_system = false;

  if (t == GeneralDialogIcon::WARNING) {
    icon_width_px = GetSystemMetrics(SM_CXICON);
    icon_height_px = GetSystemMetrics(SM_CYICON);

    if (!icon_width_px || !icon_height_px) {
      // just default to 32
      lbx_log_error("Error while doing GetSystemMetrics");
      icon_width_px = icon_height_px = 32;
    }

    icon_tag = IDI_WARNING;
  }
  else if (t == GeneralDialogIcon::NONE) {
    icon_width_px = 0;
    icon_width_px = 0;
    icon_margin = 0;
  }

  const unit_t ICON_WIDTH = icon_width_px * 4 / base_units.cx;
  const unit_t ICON_HEIGHT = icon_height_px * 8 / base_units.cy;
  const unit_t ICON_TOP = TOP_MARGIN;
  const unit_t ICON_LEFT = LEFT_MARGIN;

  // compute body width
  const unit_t ICON_MARGIN = icon_margin;
  // NB: body width should be min 33 characters,
  //     height is characters / 33
  //     otherwise it grows with # of buttons
  const unit_t MAX_CHARS_PER_LINE = 33;
  const unit_t MIN_BODY_WIDTH = 4 * MAX_CHARS_PER_LINE;
  const unit_t BUTTON_SPACING = 4;

  // RANT: this should be a fold over a map
  unit_t body_width = 0;
  for (const auto & choice : choices) {
    body_width += button_width(choice.message);
  }
  body_width += (choices.size() - 1) * BUTTON_SPACING;
  const unit_t BODY_WIDTH = std::max(MIN_BODY_WIDTH, body_width);

  const unit_t TEXT_WIDTH = BODY_WIDTH;
  const unit_t TEXT_HEIGHT = (FONT_HEIGHT * num_characters(msg) /
                              MAX_CHARS_PER_LINE);
  const unit_t TEXT_LEFT = ICON_LEFT + ICON_WIDTH + ICON_MARGIN;
  const unit_t TEXT_TOP = TOP_MARGIN;

  const unit_t BUTTON_HEIGHT = 11;

  const unit_t DIALOG_WIDTH =
    LEFT_MARGIN + ICON_WIDTH + ICON_MARGIN + BODY_WIDTH + RIGHT_MARGIN;
  const unit_t DIALOG_HEIGHT =
    TOP_MARGIN +
    std::max(ICON_HEIGHT,
             TEXT_HEIGHT + FONT_HEIGHT +
             BUTTON_HEIGHT) +
    BOTTOM_MARGIN;

  auto controls =
    std::vector<DialogItemDesc>({
        LText(msg, IDC_STATIC,
              TEXT_LEFT, TEXT_TOP,
              TEXT_WIDTH, TEXT_HEIGHT),
      });

  if (t != GeneralDialogIcon::NONE) {
    controls.push_back(Control("", IDC_LOGO, ControlClass::STATIC,
                               SS_OWNERDRAW, ICON_LEFT, ICON_TOP,
                               ICON_WIDTH, ICON_HEIGHT));
  }

  // discover button offsets
  auto left_offset = DIALOG_WIDTH - RIGHT_MARGIN;
  std::vector<decltype(left_offset)> button_offsets;
  for (const auto & choice : reversed(choices)) {
    auto width = button_width(choice.message);
    left_offset -= width;
    button_offsets.push_back(left_offset);
    left_offset -= BUTTON_SPACING;
  }

  // add all buttons
  auto BUTTON_TOP = DIALOG_HEIGHT - BOTTOM_MARGIN - BUTTON_HEIGHT;
  for (const auto & e : enumerate(range_zip(reversed(button_offsets),
                                            choices))) {
    auto left_offset = e.value.first;
    auto button_index = e.index;
    auto message = e.value.second.message;
    auto width = button_width(message);
    if (!button_index) {
      controls.push_back(DefPushButton(message,
                                       IDC_BUTTON_BASE + button_index,
                                       left_offset, BUTTON_TOP,
                                       width, BUTTON_HEIGHT));
    }
    else {
      controls.push_back(PushButton(message,
                                    IDC_BUTTON_BASE + button_index,
                                    left_offset, BUTTON_TOP,
                                    width, BUTTON_HEIGHT));
    }
  }

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              title, 0, 0,
                              DIALOG_WIDTH, DIALOG_HEIGHT,
                              FontDescription(desired_point,
                                              narrow(message_font.lfFaceName))),
                   controls);

  GeneralSafeDialogCtx ctx = {
    std::move(choices),
    std::move(close_action),
    icon_tag,
    is_system,
  };
  auto ret = DialogBoxIndirectParam(GetModuleHandle(NULL),
                                    dlg.get_data(),
                                    hwnd, general_safe_dialog_proc,
                                    (LPARAM) &ctx);
  if (ret == 0 || ret == (INT_PTR) -1) throw_windows_error();

  return receive_dialog_box_data<generic_choice_type>(ret);
}

}

}}

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

#include <lockbox/windows_gui_util.hpp>

#include <lockbox/product_name.h>
#include <lockbox/util.hpp>
#include <lockbox/windows_string.hpp>

#include <encfs/cipher/MemoryPool.h>
#include <encfs/base/optional.h>

#include <iostream>
#include <sstream>
#include <string>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace w32util {

void
quick_alert(HWND owner,
            const std::string &msg,
            const std::string &title) {
  MessageBoxW(owner,
              w32util::widen(msg).c_str(),
              w32util::widen(title).c_str(),
              MB_ICONEXCLAMATION | MB_OK);
}

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

void
center_window_in_monitor(HWND hwnd) {
  auto monitor_handle =
    MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  // there doesn't seem to be an error return for this

  // get monitor info
  MONITORINFO minfo;
  lockbox::zero_object(minfo);
  minfo.cbSize = sizeof(minfo);
  auto success = GetMonitorInfo(monitor_handle, &minfo);
  if (!success) throw std::runtime_error("Couldn't GetMonitorInfo()");

  // get window info
  RECT window_rect;
  auto success_get_window_rect = GetWindowRect(hwnd, &window_rect);
  if (!success_get_window_rect) {
    throw std::runtime_error("Couldn't get window rect");
  }

  auto horiz_center_of_monitor =
    (minfo.rcMonitor.left + minfo.rcMonitor.right) / 2;

  auto vert_center_of_monitor =
    (minfo.rcMonitor.top + minfo.rcMonitor.bottom) / 2;

  auto width_of_window = window_rect.right - window_rect.left;
  auto height_of_window = window_rect.bottom - window_rect.top;

  auto new_left = horiz_center_of_monitor - width_of_window / 2;
  auto new_top = vert_center_of_monitor - height_of_window / 2;

  // position window

  auto success_set_window_pos =
    SetWindowPos(hwnd, NULL, new_left, new_top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  if (!success_set_window_pos) {
    throw std::runtime_error("Couldn't set window pos!");
  }
}

LOGFONT
get_message_font() {
  NONCLIENTMETRICSW metrics;
  lockbox::zero_object(metrics);
  metrics.cbSize = sizeof(metrics);
  auto success = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
                                       sizeof(metrics), &metrics, 0);
  if (!success) throw windows_error();
  return std::move(metrics.lfMessageFont);
}

CALLBACK
static
BOOL
_set_default_dialog_font_enum_windows_callback(HWND hwnd, LPARAM lParam) {
  auto handle_font = (HFONT) lParam;
  // NB: there is no meaningful return code for WM_SETFONT
  SendMessage(hwnd, WM_SETFONT, (WPARAM) handle_font, (LPARAM) TRUE);
  return TRUE;
}

void
set_default_dialog_font(HWND hwnd) {
  // if there are no children, then don't create a font
  auto child = GetWindow(hwnd, GW_CHILD);
  if (!child) {
    if (GetLastError()) throw windows_error();
    else return;
  }

  auto message_font = get_message_font();
  auto handle_font = CreateFontIndirect(&message_font);
  if (!handle_font) throw windows_error();

  // set the font on all the dialog's children
  static_assert(sizeof(LPARAM) >= sizeof(handle_font),
                "LPARAM is too small or handle_font is too large");
  EnumChildWindows(hwnd, _set_default_dialog_font_enum_windows_callback,
                   (LPARAM) handle_font);
}

void
cleanup_default_dialog_font(HWND hwnd) {
  // get it from the child because the parent might be
  // handling a message
  auto child = GetWindow(hwnd, GW_CHILD);
  if (!child) {
    if (GetLastError()) throw windows_error();
    else return;
  }
  auto hfont = (HFONT) SendMessage(child, WM_GETFONT, 0, 0);
  if (!hfont) throw windows_error();
  auto success_delete_object = DeleteObject(hfont);
  if (!success_delete_object) throw windows_error();
}

void
clear_text_field(HWND text_hwnd, size_t num_chars) {
  auto zeroed_bytes =
    std::unique_ptr<wchar_t[]>(new wchar_t[num_chars + 1]);
  memset(zeroed_bytes.get(), 0xaa, num_chars * sizeof(wchar_t));
  zeroed_bytes[num_chars] = 0;
  auto success = SetWindowText(text_hwnd, zeroed_bytes.get());
  if (!success) throw w32util::windows_error();
}

std::string
read_text_field(HWND text_hwnd) {
  // read size of text in text field
  SetLastError(0);
  auto max_num_chars = GetWindowTextLengthW(text_hwnd);
  if (!max_num_chars && GetLastError()) throw w32util::windows_error();

  auto text_buf = std::unique_ptr<wchar_t[]>(new wchar_t[max_num_chars + 1]);
  SetLastError(0);
  auto num_chars = GetWindowTextW(text_hwnd, text_buf.get(), max_num_chars + 1);
  if (!num_chars && GetLastError()) throw w32util::windows_error();

  return w32util::narrow(text_buf.get(), num_chars);
}

encfs::SecureMem
securely_read_text_field(HWND text_hwnd, bool clear) {
  // read size of text in text field
  SetLastError(0);
  auto max_num_chars = GetWindowTextLengthW(text_hwnd);
  if (!max_num_chars && GetLastError()) throw w32util::windows_error();

  auto st1 = encfs::SecureMem((max_num_chars + 1) * sizeof(wchar_t));
  SetLastError(0);
  auto num_chars =
    GetWindowTextW(text_hwnd,
                   (wchar_t *) st1.data(),
                   st1.size() / sizeof(wchar_t));
  if (!num_chars && GetLastError()) throw w32util::windows_error();

  // attempt to clear what's in dialog box
  if (clear) clear_text_field(text_hwnd, num_chars);

  // convert wchars to utf8
  size_t necessary_buf_size =
    w32util::narrow_into_buf((wchar_t *) st1.data(), num_chars, NULL, 0);

  auto st2 = encfs::SecureMem(necessary_buf_size + 1);
  size_t ret =
    w32util::narrow_into_buf((wchar_t *) st1.data(), num_chars,
                             (char *) st2.data(), st2.size() - 1);

  st2.data()[ret] = '\0';

  return std::move(st2);
}

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

void
open_url_in_browser(HWND owner, std::string url) {
  auto ret_shell2 =
    (INT_PTR) ShellExecuteW(owner, L"open",
			    w32util::widen(url).c_str(),
			    NULL, NULL, SW_SHOWNORMAL);
  if (ret_shell2 <= 32) throw w32util::windows_error();
}

LONG
compute_point_size_of_logfont(HWND hwnd, const LOGFONT & lplf) {
  assert(lplf.lfHeight < 0);
  auto hDC = GetDC(hwnd);
  assert(GetMapMode(hDC) == MM_TEXT);
  if (!hDC) throw w32util::windows_error();
  auto _release_dc_1 =
    lockbox::create_deferred(ReleaseDC, hwnd, hDC);
  return MulDiv(-lplf.lfHeight, 72,
                GetDeviceCaps(hDC, LOGPIXELSY));
}

SIZE
compute_base_units_of_logfont(HWND hwnd, const LOGFONT & lplf) {
  // LOGFONT already gives us the height of the font
  // we just need to compute the average width
  auto handle_font = CreateFontIndirect(&lplf);
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

}


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

#include <lockbox/util.hpp>
#include <lockbox/windows_string.hpp>

#include <string>

#include <windows.h>

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

}

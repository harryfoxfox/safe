/*
  Safe: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <w32util/menu.hpp>

#include <safe/util.hpp>
#include <w32util/error.hpp>
#include <w32util/string.hpp>

#include <windows.h>

namespace w32util {

void
menu_append_separator(HMENU menu_handle) {
  MENUITEMINFOW mif;
  safe::zero_object(mif);

  mif.cbSize = sizeof(mif);
  mif.fMask = MIIM_FTYPE;
  mif.fType = MFT_SEPARATOR;

  auto items_added = GetMenuItemCount(menu_handle);
  if (items_added == -1) w32util::throw_windows_error();
  auto success_menu_item =
    InsertMenuItemW(menu_handle, items_added, TRUE, &mif);
  if (!success_menu_item) w32util::throw_windows_error();
}

int
menu_append_string_item(HMENU menu_handle, bool is_default,
                        std::string text, UINT id) {
  auto menu_item_text =
    w32util::widen(std::move(text));

  MENUITEMINFOW mif;
  safe::zero_object(mif);

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

  // return pos
  return items_added;
}

void
menu_clear(HMENU menu_handle) {
  auto item_count = GetMenuItemCount(menu_handle);
  if (item_count == -1) w32util::throw_windows_error();

  for (auto _ : safe::range(item_count)) {
    (void) _;
    auto success = DeleteMenu(menu_handle, 0, MF_BYPOSITION);
    if (!success) w32util::throw_windows_error();
  }
}

}

/*
  Safe: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

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

#ifndef _safe_system_changes_dialog_win_hpp
#define _safe_system_changes_dialog_win_hpp

#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace safe { namespace win {

enum class SystemChangesChoice {
  DONT_MAKE_CHANGES,
  MAKE_CHANGES,
};

std::pair<SystemChangesChoice, bool>
system_changes_dialog(HWND hwnd, std::vector<std::string> changes);

}}

#endif

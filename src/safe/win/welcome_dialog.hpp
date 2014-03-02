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

#ifndef _safe_welcome_dialog_win_hpp
#define _safe_welcome_dialog_win_hpp

#include <safe/lean_windows.h>

namespace safe { namespace win {

enum class WelcomeDialogChoice {
  NOTHING=1,
  CREATE_NEW_SAFE,
  MOUNT_EXISTING_SAFE,
};

WelcomeDialogChoice
welcome_dialog(HWND hwnd, bool installed_kernel_driver);

}}

#endif

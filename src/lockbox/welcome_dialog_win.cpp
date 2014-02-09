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
#include <lockbox/general_lockbox_dialog_win.hpp>
#include <lockbox/logging.h>
#include <lockbox/resources_win.h>
#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_gui_util.hpp>

#include <windows.h>

namespace lockbox { namespace win {

WelcomeDialogChoice
welcome_dialog(HWND hwnd) {
  std::vector<Choice<WelcomeDialogChoice>> choices = {
    {LOCKBOX_DIALOG_WELCOME_CREATE_BUTTON,
     WelcomeDialogChoice::CREATE_NEW_LOCKBOX},
    {LOCKBOX_DIALOG_WELCOME_MOUNT_BUTTON,
     WelcomeDialogChoice::MOUNT_EXISTING_LOCKBOX},
  };

  const auto & c = &general_lockbox_dialog<WelcomeDialogChoice,
                                           decltype(choices)>;
  return c(hwnd, "Welcome to Safe!", LOCKBOX_DIALOG_WELCOME_TEXT,
           std::move(choices));
}

}}

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

#include <safe/win/welcome_dialog.hpp>

#include <safe/constants.h>
#include <safe/win/dialog_common.hpp>
#include <safe/win/general_safe_dialog.hpp>
#include <safe/logging.h>
#include <safe/resources_win.h>
#include <w32util/dialog.hpp>
#include <w32util/gui_util.hpp>

#include <encfs/base/optional.h>

#include <windows.h>

namespace safe { namespace win {

WelcomeDialogChoice
welcome_dialog(HWND hwnd, bool installed_kernel_driver) {
  std::vector<Choice<WelcomeDialogChoice>> choices = {
    {SAFE_DIALOG_WELCOME_CREATE_BUTTON,
     WelcomeDialogChoice::CREATE_NEW_SAFE},
    {SAFE_DIALOG_WELCOME_MOUNT_BUTTON,
     WelcomeDialogChoice::MOUNT_EXISTING_SAFE},
  };

  auto close_action =
    ButtonAction<WelcomeDialogChoice>([]() {
        return WelcomeDialogChoice::NOTHING;
      });

  const auto & c = &general_safe_dialog<WelcomeDialogChoice,
                                           decltype(choices)>;
  return c(hwnd, "Welcome to Safe!",
           installed_kernel_driver
           ? SAFE_DIALOG_WELCOME_TEXT_POST_DRIVER_INSTALL
           : SAFE_DIALOG_WELCOME_TEXT,
           std::move(choices),
           close_action
           );
}

}}

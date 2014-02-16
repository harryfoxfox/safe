/*
  Lockbox: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

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

#include <lockbox/system_changes_dialog_win.hpp>

#include <lockbox/constants.h>
#include <lockbox/logging.h>
#include <lockbox/general_lockbox_dialog_win.hpp>
#include <lockbox/windows_gui_util.hpp>
#include <lockbox/windows_string.hpp>

#include <encfs/base/optional.h>

#include <vector>

#include <windows.h>

namespace lockbox { namespace win {

SystemChangesChoice
system_changes_dialog(HWND hwnd) {
  auto more_info = [=] () {
    try {
      w32util::open_url_in_browser(hwnd,
                                   LOCKBOX_WINDOWS_SYSTEM_CHANGES_INFO_WEBSITE);
    }
    catch (...) {
      lbx_log_error("Error opening source website");
    }
    return opt::nullopt;
  };

  auto quit = [=] () {
    auto ret = MessageBoxW(hwnd,
                           w32util::widen(LOCKBOX_DIALOG_CANCEL_SYSTEM_CHANGES_MESSAGE).c_str(),
                           w32util::widen(LOCKBOX_DIALOG_CANCEL_SYSTEM_CHANGES_TITLE).c_str(),
                           MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND);
    return (ret == IDYES
            ? opt::make_optional(SystemChangesChoice::QUIT)
            : opt::nullopt);
  };

  std::vector<Choice<SystemChangesChoice>> choices = {
    {LOCKBOX_DIALOG_SYSTEM_CHANGES_OK, SystemChangesChoice::OK},
    {LOCKBOX_DIALOG_SYSTEM_CHANGES_MORE_INFO, more_info},
  };

  const auto & c = &general_lockbox_dialog<SystemChangesChoice,
                                           decltype(choices)>;
  return c(hwnd,
           LOCKBOX_DIALOG_SYSTEM_CHANGES_TITLE,
           LOCKBOX_DIALOG_SYSTEM_CHANGES_MESSAGE,
           std::move(choices),
           ButtonAction<SystemChangesChoice>(quit));
}

}}

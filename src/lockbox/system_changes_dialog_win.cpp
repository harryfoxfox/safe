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
                           (L"These changes are essential to keep your "
                            L"data safe from attackers. If you quit "
                            L"now you won't be able to use Safe. Are you "
                            L"sure you want to quit?"),
                           L"Are You Sure?",
                           MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND);
    return (ret == IDYES
            ? opt::make_optional(SystemChangesChoice::QUIT)
            : opt::nullopt);
  };

  std::vector<Choice<SystemChangesChoice>> choices = {
    {"OK", SystemChangesChoice::OK},
    {"More Info", more_info},
  };

  const auto & c = &general_lockbox_dialog<SystemChangesChoice,
                                           decltype(choices)>;
  return c(hwnd,
           ("Welcome to " PRODUCT_NAME_A "!"),
           ("Before you can get started using " PRODUCT_NAME_A " "
            "we must make some low-level changes to your system "
            "to support greater privacy."),
           std::move(choices),
           ButtonAction<SystemChangesChoice>(quit));
}

}}

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

#include <safe/win/system_changes_dialog.hpp>

#include <safe/constants.h>
#include <safe/logging.h>
#include <safe/win/general_safe_dialog.hpp>
#include <w32util/gui_util.hpp>
#include <w32util/string.hpp>

#include <encfs/base/optional.h>

#include <vector>

#include <windows.h>

namespace safe { namespace win {

std::pair<SystemChangesChoice, bool>
system_changes_dialog(HWND hwnd, std::vector<std::string> changes) {
  assert(!changes.empty());

  auto more_info = [=] () {
    try {
      w32util::open_url_in_browser(hwnd,
                                   SAFE_WINDOWS_SYSTEM_CHANGES_INFO_WEBSITE);
    }
    catch (...) {
      lbx_log_error("Error opening source website");
    }
    return opt::nullopt;
  };

  std::vector<Choice<SystemChangesChoice>> choices = {
    // TODO: add "Admin" icon next to "Make Changes" button
    {SAFE_DIALOG_SYSTEM_CHANGES_MAKE_CHANGES, SystemChangesChoice::MAKE_CHANGES},
    {SAFE_DIALOG_SYSTEM_CHANGES_DONT_MAKE_CHANGES, SystemChangesChoice::DONT_MAKE_CHANGES},
    {SAFE_DIALOG_SYSTEM_CHANGES_MORE_INFO, more_info},
  };

  std::ostringstream os;
  os << SAFE_DIALOG_SYSTEM_CHANGES_HEADER;
  os << "\r\n\r\n";
  for (const auto & change : changes) {
    os << "    " << SAFE_DIALOG_SYSTEM_CHANGES_BULLET << " " << change << "\r\n";
  }
  os << "\r\n" << SAFE_DIALOG_SYSTEM_CHANGES_FOOTER;

  return general_safe_dialog_with_suppression<SystemChangesChoice>
    (hwnd,
     SAFE_DIALOG_SYSTEM_CHANGES_TITLE,
     os.str(),
     std::move(choices),
     opt::nullopt, GeneralDialogIcon::SAFE, true);
}

}}

/*
  Safe: Encrypted File System
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

#include <safe/win/report_bug_dialog.hpp>

#include <w32util/string.hpp>

#include <string>

#include <windows.h>

namespace safe { namespace win {

ReportBugDialogChoice
report_bug_dialog(HWND owner, std::string message) {
  // a little lazy here since this doesn't happen often,
  // TODO: better looking dialog

  auto msg = (std::string(message) +
              (" Would you like to help us improve by sending a bug report? "
               "It's automatic and no personal information is used."));

  auto ret = MessageBoxW(owner,
                         w32util::widen(msg).c_str(),
                         L"Unexpected Error Occured",
                         MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND);
  return (ret == IDYES
          ? ReportBugDialogChoice::REPORT_BUG
          : ReportBugDialogChoice::DONT_REPORT_BUG);
}

}}

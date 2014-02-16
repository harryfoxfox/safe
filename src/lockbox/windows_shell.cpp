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

#include <lockbox/windows_shell.hpp>

#include <lockbox/windows_error.hpp>
#include <lockbox/windows_string.hpp>

#include <string>

#include <windows.h>
#include <shlobj.h>

namespace w32util {

std::string
get_folder_path(int folder, DWORD flags) {
  wchar_t app_directory_buf[MAX_PATH + 1];
  w32util::check_good_call(S_OK, SHGetFolderPathW,
                           nullptr, folder, nullptr, flags,
                           app_directory_buf);
  return w32util::narrow(app_directory_buf);
}


}

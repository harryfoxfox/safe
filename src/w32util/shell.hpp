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

#ifndef __Safe__windows_shell_hpp
#define __Safe__windows_shell_hpp

#include <string>

#include <safe/lean_windows.h>

namespace w32util {

std::string
get_folder_path(int folder, DWORD flags);

void
create_shortcut(std::string path, std::string target);

std::string
get_shortcut_target(std::string path);

}

#endif

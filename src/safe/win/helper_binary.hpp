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

#ifndef __safe_win_helper_binary_hpp
#define __safe_win_helper_binary_hpp

#include <functional>
#include <string>
#include <vector>

#include <windows.h>

namespace safe { namespace win {

DWORD
run_helper_binary(bool elevate,
                  std::string path,
                  std::vector<std::string> arguments);

DWORD
helper_binary_main_no_argparse(std::string output_path,
                               std::function<DWORD()> fn);

DWORD
helper_binary_main(std::function<DWORD(std::vector<std::string>)> fn);

}}

#endif

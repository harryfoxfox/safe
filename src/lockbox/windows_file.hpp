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

#ifndef __Lockbox__windows_file_hpp
#define __Lockbox__windows_file_hpp

#include <cstddef>

#include <memory>
#include <string>

#include <lockbox/lean_windows.h>

namespace w32util {

struct Buffer {
  std::unique_ptr<uint8_t[]> ptr;
  size_t size;
};

std::string
get_temp_path();

std::string
get_temp_file_name(std::string root, std::string prefix);

Buffer
read_file(HANDLE handle, DWORD num_bytes_to_read);

bool
file_exists(std::string file_path);

bool
map_to_same_target(std::string a, std::string b);

}

#endif

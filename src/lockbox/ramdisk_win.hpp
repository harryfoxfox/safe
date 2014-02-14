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

#ifndef __Lockbox__ramdisk_win_hpp
#define __Lockbox__ramdisk_win_hpp

#include <lockbox/util_win.hpp>

#include <string>

namespace lockbox { namespace win {

bool
need_to_install_kernel_driver();

bool
install_kernel_driver();

bool
create_device_and_install_driver_native(std::string hardware_id,
					std::string inf_file_path);

typedef ManagedHandle RAMDiskHandle;

RAMDiskHandle
engage_ramdisk();

}}

#endif

/*
  Safe Ramdisk:
  A simple Windows driver to emulate a FAT32 formatted disk drive
  backed by paged memory in the Windows kernel.

  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef __safe_ramdisk_control_device_hpp
#define __safe_ramdisk_control_device_hpp

#include "ramdisk_device.hpp"

namespace safe_nt {

NTSTATUS
create_ramdisk_control_device(PDRIVER_OBJECT,
			      RAMDiskDevice *,
			      PDEVICE_OBJECT *);

void
delete_ramdisk_control_device(PDEVICE_OBJECT);

}

#endif

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

#ifndef __safe_ramdisk_ioctl_h
#define __safe_ramdisk_ioctl_h

#ifndef CTL_CODE
#define CTL_CODE(DeviceType, Function, Method, Access)( \
  ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif

#ifndef METHOD_BUFFERED
#define METHOD_BUFFERED 0
#endif

#define FILE_DEVICE_SAFE_RAMDISK_CTL 0x8373

#define IOCTL_SAFE_RAMDISK_ENGAGE \
  ((ULONG) CTL_CODE(FILE_DEVICE_SAFE_RAMDISK_CTL, 0x800, METHOD_BUFFERED, 0))

#define IOCTL_SAFE_RAMDISK_DISENGAGE \
  ((ULONG) CTL_CODE(FILE_DEVICE_SAFE_RAMDISK_CTL, 0x801, METHOD_BUFFERED, 0))

#define RAMDISK_CTL_DOSNAME_W L"SafeDos"

#define SAFE_RAMDISK_SIZE_VALUE_NAME_W L"RAMDiskSize"

#endif

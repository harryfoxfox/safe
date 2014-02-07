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

#ifndef __safe_io_device_hpp
#define __safe_io_device_hpp

#include <ntifs.h>

namespace safe_nt {

class IODevice {
public:
  virtual
  NTSTATUS
  irp_create(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_cleanup(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_close(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_read(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_write(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_device_control(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_pnp(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_power(PIRP irp) noexcept;

  virtual
  NTSTATUS
  irp_system_control(PIRP irp) noexcept;
};

}

#endif

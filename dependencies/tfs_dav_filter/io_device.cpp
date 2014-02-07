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

#include "io_device.hpp"
#include "nt_helpers.hpp"

#include <ntifs.h>

namespace safe_nt {

static
NTSTATUS
_not_supported(PIRP irp) {
  return standard_complete_irp(irp, STATUS_INVALID_DEVICE_REQUEST);
}

NTSTATUS
IODevice::irp_create(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_cleanup(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_close(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_read(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_write(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_device_control(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_pnp(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_power(PIRP irp) {
  return _not_supported(irp);
}

NTSTATUS
IODevice::irp_system_control(PIRP irp) {
  return _not_supported(irp);
}

}



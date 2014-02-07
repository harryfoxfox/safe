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

#include "io_device.hpp"

#include <lockbox/deferred.hpp>

#include <ntifs.h>

namespace safe_nt {

class RAMDiskControlDevice : public IODevice {
  typedef
  decltype(lockbox::create_deferred(KeReleaseMutex,
				    (KMUTEX *) nullptr,
				    FALSE))
  EngageLockGuard;

  KMUTEX _engage_mutex;
  int _engage_count;
  HANDLE _reparse_handle;

  RAMDiskControlDevice(NTSTATUS *out) noexcept;
  ~RAMDiskControlDevice() noexcept;

  NTSTATUS
  _create_engage_lock_guard(EngageLockGuard *out);

  NTSTATUS
  _engage();

  NTSTATUS
  _disengage();

public:
  // NB: As per: http://msdn.microsoft.com/en-us/library/windows/hardware/ff556796%28v=vs.85%29.aspx

  virtual
  NTSTATUS
  irp_create(PIRP irp) noexcept override;

  virtual
  NTSTATUS
  irp_cleanup(PIRP irp) noexcept override;

  virtual
  NTSTATUS
  irp_close(PIRP irp) noexcept override;

  virtual
  NTSTATUS
  irp_device_control(PIRP irp) noexcept override;

  friend
  NTSTATUS
  create_control_device(PDRIVER_OBJECT, PDEVICE_OBJECT, PDEVICE_OBJECT *);
  friend void delete_control_device(PDEVICE_OBJECT);
};

NTSTATUS
create_control_device(PDRIVER_OBJECT, PDEVICE_OBJECT, PDEVICE_OBJECT *);

void
delete_control_device(PDEVICE_OBJECT);
}

#endif

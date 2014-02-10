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

#include "ramdisk_device.hpp"

#include "nt_helpers.hpp"
#include "ramdisk_ioctl.h"

namespace safe_nt {

#ifdef _WIN64
#define DOS_DEVICES_PREFIX_W L"\\DosDevices\\Global\\"
#else
#define DOS_DEVICES_PREFIX_W L"\\DosDevices\\"
#endif

const WCHAR DOS_DEVICE_NAME[] = (DOS_DEVICES_PREFIX_W
                                 RAMDISK_CTL_DOSNAME_W);

class RAMDiskControlDevice : public IODevice {
  RAMDiskDevice *_dcb;

  RAMDiskControlDevice(RAMDiskDevice *dcb,
		       NTSTATUS *status) noexcept;

public:
  NTSTATUS
  irp_create(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_close(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_cleanup(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_device_control(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  friend
  void
  delete_ramdisk_control_device(PDEVICE_OBJECT device_object);

  friend
  NTSTATUS
  create_ramdisk_control_device(PDRIVER_OBJECT driver_object,
				RAMDiskDevice *dcb,
				PDEVICE_OBJECT *out);
};

RAMDiskControlDevice::RAMDiskControlDevice(RAMDiskDevice *dcb,
					   NTSTATUS *status) noexcept
  : _dcb(dcb) {
  *status = STATUS_SUCCESS;
}

NTSTATUS
RAMDiskControlDevice::irp_create(PIRP irp) noexcept {
  return _dcb->irp_create(irp);
}

NTSTATUS
RAMDiskControlDevice::irp_close(PIRP irp) noexcept {
  return _dcb->irp_close(irp);
}

NTSTATUS
RAMDiskControlDevice::irp_cleanup(PIRP irp) noexcept {
  return _dcb->control_device_cleanup(irp);
}

NTSTATUS
RAMDiskControlDevice::irp_device_control(PIRP irp) noexcept {
  return _dcb->irp_device_control(irp);
}

void
delete_ramdisk_control_device(PDEVICE_OBJECT device_object) {
  auto cdo = static_cast<RAMDiskControlDevice *>(device_object->DeviceExtension);
  cdo->~RAMDiskControlDevice();
  {
    UNICODE_STRING sym_link;
    RtlInitUnicodeString(&sym_link, DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&sym_link);
  }
  IoDeleteDevice(device_object);
}

NTSTATUS
create_ramdisk_control_device(PDRIVER_OBJECT driver_object,
			      RAMDiskDevice *dcb,
			      PDEVICE_OBJECT *out) {
  UNICODE_STRING device_name;
  RtlInitUnicodeString(&device_name, L"\\Device\\SafeRamDiskCtl");

  auto status = IoCreateDevice(driver_object,
			       sizeof(RAMDiskControlDevice),
			       &device_name,
			       FILE_DEVICE_SAFE_RAMDISK_CTL,
			       0,
			       FALSE,
			       out);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while calling IoCreateDevice: %s (0x%x)\n",
		 nt_status_to_string(status), status);
    return status;
  }

  auto _delete_device_object =
    lockbox::create_deferred(delete_ramdisk_control_device, *out);
  
  NTSTATUS status3;
  new ((*out)->DeviceExtension) RAMDiskControlDevice(dcb, &status3);
  if (!NT_SUCCESS(status3)) {
    nt_log_error("Error while calling RAMDiskControlDevice()\n");
    return status3;
  }

  {
    // Create device symlink
    UNICODE_STRING symbolic_link_name;
    RtlInitUnicodeString(&symbolic_link_name, DOS_DEVICE_NAME);
    auto status5 = IoCreateSymbolicLink(&symbolic_link_name, &device_name);
    if (!NT_SUCCESS(status5)) return status5;
  }

  _delete_device_object.cancel();
  (*out)->Flags &= ~DO_DEVICE_INITIALIZING;

  return STATUS_SUCCESS;
}

}

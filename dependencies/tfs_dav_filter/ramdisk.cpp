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

#include "ntoskrnl_cpp.hpp"
#include "io_device.hpp"
#include "nt_helpers.hpp"
#include "ramdisk_device.hpp"
#include "ramdisk_control_device.hpp"

#include <lockbox/deferred.hpp>
#include <lockbox/low_util.hpp>

#include <ntdddisk.h>
#include <ntifs.h>

template<NTSTATUS (safe_nt::IODevice::*p)(PIRP)>
static
NTSTATUS
qd(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  auto dev = static_cast<safe_nt::IODevice *>(DeviceObject->DeviceExtension);
  return (dev->*p)(Irp);
}

extern "C" {

static
NTSTATUS
NTAPI
SafeRamDiskDispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_create>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_close>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_cleanup>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_read>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_write>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_device_control>(DeviceObject, Irp);
}

static
PDEVICE_OBJECT
g_ramdisk_control_device = nullptr;

static
NTSTATUS
NTAPI
SafeRamDiskDispatchPnP(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  // save minor function for dispatching pnp, stack location may become
  // invalidated by IoSkipCurrentIrpStackLocation
  auto minor_function = IoGetCurrentIrpStackLocation(Irp)->MinorFunction;
  auto status = qd<&safe_nt::IODevice::irp_pnp>(DeviceObject, Irp);
  nt_log_debug("IRP_MJ_PNP %s (0x%x) -> %s (0x%x)",
	       safe_nt::pnp_minor_function_to_string(minor_function),
	       minor_function,
	       safe_nt::nt_status_to_string(status),
	       status);
  if (IRP_MN_REMOVE_DEVICE == minor_function) {
    if (g_ramdisk_control_device) {
      nt_log_debug("Deleting control device: %p", g_ramdisk_control_device);
      safe_nt::delete_control_device(g_ramdisk_control_device);
    }
    safe_nt::delete_ramdisk_device(DeviceObject);
  }
  return status;
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchPower(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_power>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<&safe_nt::IODevice::irp_system_control>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskAddDevice(PDRIVER_OBJECT DriverObject, 
		     PDEVICE_OBJECT PhysicalDeviceObject) {
  // First Create Ramdisk Device
  PDEVICE_OBJECT ramdisk_device;
  auto status = 
    safe_nt::create_ramdisk_device(DriverObject, PhysicalDeviceObject,
				   &ramdisk_device);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while create_ramdisk_device\n");
    return status;
  }

  auto _delete_ramdisk_device =
    lockbox::create_deferred(safe_nt::delete_ramdisk_device, ramdisk_device);

  // Create Control Device for Ramdisk
  auto status2 = safe_nt::create_control_device(DriverObject, ramdisk_device,
						&g_ramdisk_control_device);
  if (!NT_SUCCESS(status2)) {
    nt_log_error("Error while create_control_device\n");
    return status2;
  }

  _delete_ramdisk_device.cancel();

  return STATUS_SUCCESS;
}

static
void
NTAPI
SafeRamDiskUnload(PDRIVER_OBJECT DriverObject) { 
  nt_log_debug("Unloading driver...");
}

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath) {
  nt_log_info("Safe RAMDisk\n");
  nt_log_info("Built on %s %s\n", __DATE__, __TIME__);

  // Set up callbacks
  DriverObject->MajorFunction[IRP_MJ_CREATE] = SafeRamDiskDispatchCreate;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = SafeRamDiskDispatchClose;
  DriverObject->MajorFunction[IRP_MJ_CLEANUP] = SafeRamDiskDispatchCleanup;
  DriverObject->MajorFunction[IRP_MJ_READ] = SafeRamDiskDispatchRead;
  DriverObject->MajorFunction[IRP_MJ_WRITE] = SafeRamDiskDispatchWrite;
  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SafeRamDiskDispatchDeviceControl;
  DriverObject->MajorFunction[IRP_MJ_PNP] = SafeRamDiskDispatchPnP;
  DriverObject->MajorFunction[IRP_MJ_POWER] = SafeRamDiskDispatchPower;
  DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = SafeRamDiskDispatchSystemControl;
  DriverObject->DriverExtension->AddDevice = (PVOID) SafeRamDiskAddDevice;
  DriverObject->DriverUnload = SafeRamDiskUnload;

  nt_log_debug("Loading done, returning control...");

  // Return
  return STATUS_SUCCESS;
}

}

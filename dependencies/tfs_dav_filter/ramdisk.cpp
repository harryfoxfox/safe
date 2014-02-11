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

#include <lockbox/deferred.hpp>
#include <lockbox/low_util.hpp>

#include <ntdddisk.h>
#include <ntifs.h>

template<class DeviceType, NTSTATUS (DeviceType::*p)(PIRP)>
static
NTSTATUS
qd(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  auto dev = static_cast<DeviceType *>(DeviceObject->DeviceExtension);
  return (dev->*p)(Irp);
}

extern "C" {

typedef safe_nt::IODevice qt;

static
NTSTATUS
NTAPI
SafeRamDiskDispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_create>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_close>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_cleanup>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_read>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_write>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_device_control>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchPnP(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  // save minor function for dispatching pnp, stack location may become
  // invalidated by IoSkipCurrentIrpStackLocation
  auto minor_function = IoGetCurrentIrpStackLocation(Irp)->MinorFunction;
  auto status = qd<qt, &qt::irp_pnp>(DeviceObject, Irp);
  nt_log_debug("IRP_MJ_PNP %s (0x%x) -> %s (0x%x)",
	       safe_nt::pnp_minor_function_to_string(minor_function),
	       minor_function,
	       safe_nt::nt_status_to_string(status),
	       status);
  if (IRP_MN_REMOVE_DEVICE == minor_function) {
    safe_nt::delete_ramdisk_device(DeviceObject);
  }
  return status;
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchPower(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_power>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  return qd<qt, &qt::irp_system_control>(DeviceObject, Irp);
}

UNICODE_STRING g_registry_path;

static
NTSTATUS
NTAPI
SafeRamDiskAddDevice(PDRIVER_OBJECT DriverObject, 
		     PDEVICE_OBJECT PhysicalDeviceObject) {
  return safe_nt::create_ramdisk_device(DriverObject,
                                        &g_registry_path,
					PhysicalDeviceObject);
}

static
void
NTAPI
SafeRamDiskUnload(PDRIVER_OBJECT DriverObject) { 
  ExFreePoolWithTag((PVOID) g_registry_path.Buffer, 0x19862014);
  nt_log_debug("Unloading driver...");
}

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath) {
  nt_log_info("Safe RAMDisk\n");
  nt_log_info("Built on %s %s\n", __DATE__, __TIME__);

  g_registry_path.Buffer = (PWSTR) ExAllocatePoolWithTag(PagedPool,
                                                         RegistryPath->Length,
                                                         0x19862014);
  if (!g_registry_path.Buffer) {
    nt_log_info("Allocation for registry_path copy failed");
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  memcpy(g_registry_path.Buffer, RegistryPath->Buffer,
         RegistryPath->Length);
  g_registry_path.Length = RegistryPath->Length;
  g_registry_path.MaximumLength = RegistryPath->Length;

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
  DriverObject->DriverExtension->AddDevice =
    (decltype(DriverObject->DriverExtension->AddDevice))
    SafeRamDiskAddDevice;
  DriverObject->DriverUnload = SafeRamDiskUnload;

  nt_log_debug("Loading done, returning control...");

  // Return
  return STATUS_SUCCESS;
}

}

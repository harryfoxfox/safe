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

#ifndef __safe_ramdisk_device_hpp
#define __safe_ramdisk_device_hpp

#include "io_device.hpp"
#include "ntoskrnl_cpp.hpp"

#include <safe/deferred.hpp>

#include <ntdddisk.h>
#include <ntddk.h>

namespace safe_nt {

enum class PnPState {
  NOT_STARTED,
  STARTED,
  STOP_PENDING,
  STOPPED,
  REMOVE_PENDING,
  SURPRISE_REMOVE_PENDING,
  DELETED,
};

class RAMDiskDevice : public IODevice {
  static void _free_remove_lock(RAMDiskDevice *dcb);

  typedef
  decltype(safe::create_deferred(_free_remove_lock,
				    (RAMDiskDevice *) nullptr))
  RemoveLockGuard;

  uint32_t magic;

  PDEVICE_OBJECT lower_device_object;

  HANDLE section_handle;
  LARGE_INTEGER image_size;
  DISK_GEOMETRY geom;

  UCHAR partition_type;

  LIST_ENTRY list_head;
  KSPIN_LOCK list_lock;
  KEVENT request_event;

  KEVENT terminate_thread;
  KEVENT thread_terminated;
  bool thread_started;
  PVOID thread_ref;

  IO_REMOVE_LOCK remove_lock;
  PnPState pnp_state;

  PDEVICE_OBJECT control_device;
  ULONG engage_count;

  ULONGLONG
  get_image_size() const noexcept;

  void
  queue_request(PIRP irp) noexcept;


  NTSTATUS
  dequeue_request(PIRP *out, PLARGE_INTEGER timeout = nullptr) noexcept;

  UCHAR
  get_partition_type() const noexcept;

  VOID
  worker_thread() noexcept;

  NTSTATUS
  _irp_read_or_write(PIRP irp) noexcept;

  NTSTATUS
  create_remove_lock_guard(RemoveLockGuard *);

  NTSTATUS
  acquire_remove_lock();

  void
  release_remove_lock();

  void
  release_remove_lock_and_wait();

  void
  set_pnp_state(PnPState s) {
    pnp_state = s;
  }

  PnPState
  get_pnp_state() {
    return pnp_state;
  }

  bool
  ramdisk_is_engaged() noexcept;

  PDEVICE_OBJECT
  get_device_object();

  NTSTATUS
  queue_delete_tfs_dav_children();

  RAMDiskDevice(PDRIVER_OBJECT driver_object,
		PDEVICE_OBJECT lower_device_object,
                PLARGE_INTEGER ramdisk_size,
		NTSTATUS *status) noexcept;

  ~RAMDiskDevice() noexcept;

public:
  NTSTATUS
  irp_create(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_close(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_read(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_write(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_device_control(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_pnp(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_power(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  irp_system_control(PIRP irp) noexcept IO_DEVICE_OVERRIDE;

  NTSTATUS
  control_device_cleanup(PIRP irp) noexcept;

  friend VOID NTAPI worker_thread_bootstrap(PVOID ctx) noexcept;
  friend
  NTSTATUS
  create_ramdisk_device(PDRIVER_OBJECT driver_object,
			PDEVICE_OBJECT physical_device_object) noexcept;
  friend void delete_ramdisk_device(PDEVICE_OBJECT device_object) noexcept;
};

const WCHAR RAMDISK_DEVICE_NAME[] = L"\\Device\\SafeRamDisk";

NTSTATUS
create_ramdisk_device(PDRIVER_OBJECT driver_object,
		      PDEVICE_OBJECT physical_device_object) noexcept;

void
delete_ramdisk_device(PDEVICE_OBJECT device_object) noexcept;

}

#endif

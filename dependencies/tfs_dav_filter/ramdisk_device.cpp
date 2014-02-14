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
/*
  Portions of this source file were derived from the ImDisk project.
  See the LICENSE file for copying info.
 */

#include "ramdisk_device.hpp"

#include "ntoskrnl_cpp.hpp"
#include "nt_helpers.hpp"
#include "ramdisk_control_device.hpp"
#include "ramdisk_ioctl.h"
#include "tfs_dav_reparse_engage.hpp"

#include <lockbox/deferred.hpp>
#include <lockbox/low_util.hpp>

#include <ntdddisk.h>
#include <ntddk.h>

#ifndef OBJ_KERNEL_HANDLE
#define OBJ_KERNEL_HANDLE       0x00000200
#endif

namespace safe_nt {

// this is the max amount we're willing to commit
// this is a function of the page file size
// 512MB is a conservative guess
// (assuming page files range from 1024MB to 4096MB)
// this does limit the size of files you can store in your safe
const auto DEFAULT_MEM_SIZE = 512ULL * 1024ULL * 1024ULL;

static_assert(DEFAULT_MEM_SIZE < 4096ULL * 1024ULL * 1024ULL,
              "DEFAULT MEM SIZE IS TOO LARGE");

#if !defined(__MINGW64_VERSION_MAJOR) && defined(__MINGW32_MAJOR_VERSION)

// fix for mingw32
#define WaitAny 1
// we explicitly use the Exf- prefix because some MinGW32 versions
// incorrectly export ExInterlockedInsertTailList as STDCALL
// when it's really a synonym for FASTCALL ExfInterlockedInsertTailList
#define ExInterlockedInsertTailList ExfInterlockedInsertTailList
#define ExInterlockedRemoveHeadList ExfInterlockedRemoveHeadList

#endif

#ifndef PLUGPLAY_REGKEY_DRIVER
#define PLUGPLAY_REGKEY_DRIVER                            2
#endif

#ifndef ZwCurrentProcess
#define ZwCurrentProcess() ((HANDLE)(LONG_PTR) -1)
#endif

const ULONG REMOVE_LOCK_TAG = 0x02051986UL;
const auto DISK_CONTROL_BLOCK_MAGIC = (uint32_t) 0x02051986UL;
const auto ERROR_RETRY_INTERVAL_100ns =
  2 * 1000 * 1000 * 1000 / 100;
const auto TFS_DAV_EXPIRATION_AGE_100ns =
  1 * 1000 * 1000 * 1000 / 100;
const auto FAT_WRITE_TIME_PRECISION_100ns = 
  2 * 1000 * 1000 * 1000 / 100;

void
RAMDiskDevice::_free_remove_lock(RAMDiskDevice *dcb) {
  dcb->release_remove_lock();
}

NTSTATUS
RAMDiskDevice::create_remove_lock_guard(RAMDiskDevice::RemoveLockGuard *out) {
  auto status = this->acquire_remove_lock();
  if (!NT_SUCCESS(status)) return status;
  *out = lockbox::create_deferred(_free_remove_lock, this);
  return STATUS_SUCCESS;
}

NTSTATUS
RAMDiskDevice::acquire_remove_lock() {
  auto status = IoAcquireRemoveLock(&this->remove_lock, 0);
  if (!NT_SUCCESS(status)) {
    nt_log_error("IoAcquireRemoveLock error: %s (0x%x)\n",
		 nt_status_to_string(status), status);
  }
  return status;
}

void
RAMDiskDevice::release_remove_lock() {
  IoReleaseRemoveLock(&this->remove_lock, 0);
}

void
RAMDiskDevice::release_remove_lock_and_wait() {
  IoReleaseRemoveLockAndWait(&this->remove_lock, 0);
}

VOID
NTAPI
worker_thread_bootstrap(PVOID ctx) noexcept {
  auto dcb = static_cast<RAMDiskDevice *>(ctx);
  dcb->worker_thread();
}

static
NTSTATUS
write_section(HANDLE section, size_t offset,
              const void *data, size_t data_size);

static
NTSTATUS
read_section(HANDLE section, size_t offset,
             void *data, size_t to_transfer);

static
NTSTATUS
format_fat32(HANDLE section, PLARGE_INTEGER memsize,
             PDISK_GEOMETRY pgeom,
	     PUCHAR ppartition_type);

template <bool, typename T>
struct _limit_to {
  static ULONGLONG lim(ULONGLONG v) {
    return v;
  }
};

template <typename T>
struct _limit_to<true, T> {
  static
  ULONGLONG
  lim(ULONGLONG v) {
    return std::min(v, 1ULL << (sizeof(T) * 8));
  }
};

template <typename T>
using limit_to = _limit_to<sizeof(T) < sizeof(ULONGLONG), T>;

RAMDiskDevice::RAMDiskDevice(PDRIVER_OBJECT driver_object,
                             PDEVICE_OBJECT lower_device_object,
                             PLARGE_INTEGER ramdisk_size,
			     NTSTATUS *out) noexcept {
  this->engage_count = 0;
  this->thread_ref = nullptr;
  this->lower_device_object = lower_device_object;

  this->image_size = *ramdisk_size;

  // Don't surpass the size of an address space
  this->image_size.QuadPart =
    limit_to<SIZE_T>::lim((ULONGLONG) this->image_size.QuadPart);

  this->section_handle = nullptr;
  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     nullptr,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);
  auto status3 = ZwCreateSection(&this->section_handle,
                                 SECTION_ALL_ACCESS,
                                 &attributes,
                                 &this->image_size,
                                 PAGE_READWRITE,
                                 SEC_RESERVE,
                                 nullptr);
  if (!NT_SUCCESS(status3)) {
    nt_log_error("Error while doing ZwCreateSection: %s (0x%x)",
                 nt_status_to_string(status3), status3);
    *out = status3;
    return;
  }

  auto status42 = format_fat32(this->section_handle, &this->image_size,
                              &this->geom, &this->partition_type);
  if (!NT_SUCCESS(status42)) {
    *out = status42;
    return;
  }

  InitializeListHead(&this->list_head);
  KeInitializeSpinLock(&this->list_lock);
  KeInitializeEvent(&this->request_event,
		    SynchronizationEvent, FALSE);
  KeInitializeEvent(&this->terminate_thread,
		    NotificationEvent, FALSE);

  IoInitializeRemoveLock(&this->remove_lock,
			 REMOVE_LOCK_TAG,
			 1, 0);

  this->pnp_state = PnPState::NOT_STARTED;

  auto status5 = 
    create_ramdisk_control_device(driver_object, this,
				  &this->control_device);
  if (!NT_SUCCESS(status5)) {
    *out = status5;
    return;
  }

  // Start Read / Write Thread handler
  // NB: we need to dispatch some IRPs to another thread
  //     since IRPs could run at a high IRQL level and
  //     we're using paged memory
  HANDLE thread_handle;
  auto status4 = PsCreateSystemThread(&thread_handle,
				      (ACCESS_MASK) 0L,
				      NULL,
				      NULL,
				      NULL,
				      worker_thread_bootstrap,
				      this);
  if (!NT_SUCCESS(status4)) {
    nt_log_error("Error while calling PsCreateSystemThread: 0x%x\n",
	      (unsigned) status4);
    *out = status4;
    return;
  }
  ObReferenceObjectByHandle(thread_handle, (ACCESS_MASK) 0L,
			    NULL, KernelMode, &this->thread_ref,
			    NULL);
  ZwClose(thread_handle);

  *out = STATUS_SUCCESS;
}

RAMDiskDevice::~RAMDiskDevice() noexcept {
  // signal for thread to die
  if (this->thread_ref) {
    nt_log_debug("Shutting down thread...\n");
    KeSetEvent(&this->terminate_thread, (KPRIORITY) 0, FALSE);

    // wait for thread to die
    auto status =
      KeWaitForSingleObject(this->thread_ref, Executive,
			    KernelMode, FALSE, NULL);
    if (STATUS_WAIT_0 != status) {
      nt_log_error("error while waiting on thread to die: 0x%x\n",
		   (unsigned) status);
    }
    ObDereferenceObject(this->thread_ref);
  }

  if (this->control_device) {
    delete_ramdisk_control_device(this->control_device);
  }

  // free image buffer
  if (this->section_handle) {
    auto status = ZwClose(this->section_handle);
    if (!NT_SUCCESS(status)) {
      nt_log_error("couldn't free disk virtual memory: 0x%x\n",
		   (unsigned) status);
    }
    else this->section_handle = nullptr;
  }

  if (this->lower_device_object) {
    IoDetachDevice(this->lower_device_object);
  }
}

ULONGLONG
RAMDiskDevice::get_image_size() const noexcept {
  return image_size.QuadPart;
}

void
RAMDiskDevice::queue_request(PIRP irp) noexcept {
  ExInterlockedInsertTailList(&this->list_head,
			      &irp->Tail.Overlay.ListEntry,
			      &this->list_lock);
  
  KeSetEvent(&this->request_event, (KPRIORITY) 0, FALSE);
}

NTSTATUS
RAMDiskDevice::dequeue_request(PIRP *out,
                               PLARGE_INTEGER timeout) noexcept {
  while (true) {
    auto request = ExInterlockedRemoveHeadList(&this->list_head,
					       &this->list_lock);
    if (!request) {
      // No request =>
      // wait for a request event or the terminate thread event
      PKEVENT wait_objects[] = {
	&this->request_event,
	&this->terminate_thread,
      };
      
      auto status =
	KeWaitForMultipleObjects(lockbox::numelementsf(wait_objects),
				 (PVOID *) wait_objects,
				 WaitAny,
				 Executive,
				 KernelMode,
				 FALSE,
				 timeout,
				 NULL);
      if (status == STATUS_WAIT_1) {
        *out = nullptr;
        break;
      }

      if (status != STATUS_WAIT_0) return status;
      else continue;
    }
    
    *out = CONTAINING_RECORD(request, IRP, Tail.Overlay.ListEntry);
    break;
  }

  return STATUS_SUCCESS;
}

UCHAR
RAMDiskDevice::get_partition_type() const noexcept {
  return PARTITION_FAT32;
}

bool
RAMDiskDevice::ramdisk_is_engaged() noexcept {
  return this->engage_count;
}
  
static
void
set_engaged(PFILE_OBJECT file_object, bool engaged) {
  file_object->FsContext = (PVOID) engaged;
}

static
bool
get_engaged(PFILE_OBJECT file_object) {
  return (bool) file_object->FsContext;
}

static
VOID
NTAPI
delete_tfs_dav_children_work_item(PDEVICE_OBJECT device_object,
                                  PVOID ctx) {
  IoFreeWorkItem((PIO_WORKITEM) ctx);
  auto status = delete_tfs_dav_children(TFS_DAV_EXPIRATION_AGE_100ns);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Failed to call delete_tfs_dav_children: %s (0x%x)",
                 nt_status_to_string(status), status);
  }
}

PDEVICE_OBJECT
RAMDiskDevice::get_device_object() {
  // TODO: may have to change this to the actual device object
  return lower_device_object;
}

NTSTATUS
RAMDiskDevice::queue_delete_tfs_dav_children() {
  nt_log_debug("queueing new children delete job");

  auto work_item = IoAllocateWorkItem(get_device_object());
  if (!work_item) {
    nt_log_error("Error IoAllocateWorkItem: not enough resources\n");
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  IoQueueWorkItem(work_item, delete_tfs_dav_children_work_item,
                  DelayedWorkQueue, (PVOID) work_item);

  return STATUS_SUCCESS;
}

VOID
RAMDiskDevice::worker_thread() noexcept {
  auto dcb = this;

  nt_log_info("Worker thread, rock and roll!\n");

  // NB: dcb->engage_count can be read by any thread
  // but is only written by this thread,
  // therefore we don't really need to use the Interlocked* API
  
  HANDLE reparse_handle = nullptr;
  
  LARGE_INTEGER timeout_val;
  PLARGE_INTEGER timeout = nullptr;
  while (true) {
    PIRP irp;
    
    // not sure if the relative time specified in
    // KeWaitForMultipleObjects's timeout parameter decrements
    // while the system is asleep or not,
    // -> guessing here that it is, so we use KeQueryInterruptTime
    //    (instead of KeUnbiasedQueryInterruptTime) if the guess
    //    is wrong it's fine, the worst that could happen is
    //    schedule a spurious delete job
    auto start_time = KeQueryInterruptTime();
    auto status = dcb->dequeue_request(&irp, timeout);
    auto elapsed = KeQueryInterruptTime() - start_time;
    if (timeout) {
      // decrement timeout
      timeout->QuadPart = -(-timeout->QuadPart - elapsed);
      if (timeout->QuadPart > 0) timeout->QuadPart = 0;
    }

    if (!NT_SUCCESS(status)) {
      // another random error, this is quite bad
      // the best we can do is try again
      // we'll wait before doing so
      LARGE_INTEGER wait_timeout;
      wait_timeout.QuadPart = -ERROR_RETRY_INTERVAL_100ns;
      KeDelayExecutionThread(KernelMode, FALSE, &wait_timeout);
      continue;
    }

    if (status == STATUS_TIMEOUT) {
      dcb->queue_delete_tfs_dav_children();
      timeout = nullptr;
      continue;
    }
    else if (status != STATUS_SUCCESS) {
      // something else happened in the wait just spin
      continue;
    }

    // Got terminate message
    if (!irp) break;

    nt_log_debug("Got new request!\n");

    // queue a remove lock to be freed after this request is done
    auto _free_lock =
      lockbox::create_deferred(_free_remove_lock, this);

    // service request
    auto io_stack = IoGetCurrentIrpStackLocation(irp);

    // helper function for disengage
    auto disengage_with_count = [&] () {
      NTSTATUS status = STATUS_SUCCESS;
      
      assert(dcb->engage_count);

      if (dcb->engage_count == 1)  {
	assert(reparse_handle);
	status = disengage_reparse_point(reparse_handle);
	if (NT_SUCCESS(status)) reparse_handle = nullptr;
      }
	  
      if (NT_SUCCESS(status)) {
	dcb->engage_count -= 1;
	set_engaged(io_stack->FileObject, false);
      }

      return status;
    };

    switch (io_stack->MajorFunction) {
    case IRP_MJ_READ: case IRP_MJ_WRITE: {
      // NB: io_stack->Parameters.Write is the exact same structure as
      //     io_stack->Parameters.Read in the Parameters union
      //     (and will always be since the ABI is standardized)
      //     so just use Read

      auto system_buffer =
        (PUCHAR) MmGetSystemAddressForMdlSafe(irp->MdlAddress,
                                              NormalPagePriority);
      if (!system_buffer) {

        irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        break;
      }

      SIZE_T to_transfer;
      assert(io_stack->Parameters.Read.ByteOffset.QuadPart >= 0);
      if ((decltype(dcb->get_image_size())) io_stack->Parameters.Read.ByteOffset.QuadPart <
	  dcb->get_image_size()) {

	SIZE_T vm_offset = io_stack->Parameters.Read.ByteOffset.QuadPart;
	static_assert(sizeof(SIZE_T) >= sizeof(io_stack->Parameters.Read.Length),
		      "SIZE_T is too small!");
	to_transfer = (SIZE_T) std::min((ULONGLONG) io_stack->Parameters.Read.Length,
                                        dcb->get_image_size() - vm_offset);

	nt_log_debug("%s AT %lu -> %lu\n",
                     io_stack->MajorFunction == IRP_MJ_READ ? "READ" : "WRITE",
                     (unsigned long) vm_offset, (unsigned long) to_transfer);
        NTSTATUS status2;
	if (io_stack->MajorFunction == IRP_MJ_READ) {
          status2 = read_section(this->section_handle, vm_offset,
                                 system_buffer, to_transfer);
	}
	else {
          status2 = write_section(this->section_handle, vm_offset,
                                  system_buffer, to_transfer);
	}

        if (!NT_SUCCESS(status2)) {
          irp->IoStatus.Status = status2;
          irp->IoStatus.Information = 0;
          break;
        }
      }
      else to_transfer = 0;

      if (to_transfer != io_stack->Parameters.Read.Length) {
	nt_log_debug("Short transfer, wanted: %lu, got %lu",
		  io_stack->Parameters.Read.Length, to_transfer);
      }
      
      irp->IoStatus.Status = STATUS_SUCCESS;
      irp->IoStatus.Information = to_transfer;

      if (io_stack->MajorFunction == IRP_MJ_WRITE &&
          dcb->ramdisk_is_engaged()) {
        // we just had a write, so schedule a delete
        timeout_val.QuadPart = -(TFS_DAV_EXPIRATION_AGE_100ns +
                                 FAT_WRITE_TIME_PRECISION_100ns);
        timeout = &timeout_val;
      }

      break;
    }

    case IRP_MJ_DEVICE_CONTROL: {
      auto ioctl = io_stack->Parameters.DeviceIoControl.IoControlCode;

      nt_log_debug("async device_control_irp: IOCTL: %s (0x%x)\n",
		   ioctl_to_string(ioctl),
		   (unsigned) ioctl);

      switch (ioctl) {
      case IOCTL_SAFE_RAMDISK_ENGAGE: {
	NTSTATUS status = STATUS_SUCCESS;
	
	if (get_engaged(io_stack->FileObject)) {
	  status = STATUS_INVALID_DEVICE_STATE;
	}
	else {
	  if (dcb->engage_count == 0) {
	    status = engage_reparse_point(&reparse_handle);
	  }
	  
	  if (NT_SUCCESS(status)) {
	    dcb->engage_count += 1;
	    set_engaged(io_stack->FileObject, true);
	  }
	}
	
	irp->IoStatus.Status = status;
	break;
      }
	 
      case IOCTL_SAFE_RAMDISK_DISENGAGE: {
	irp->IoStatus.Status = get_engaged(io_stack->FileObject)
	  ? disengage_with_count()
	  : STATUS_INVALID_DEVICE_STATE;
	break;
      }

      default: {
	irp->IoStatus.Status = STATUS_DRIVER_INTERNAL_ERROR;
	break;
      }
      }
      
      {
	auto status = irp->IoStatus.Status;
	if (NT_SUCCESS(status)) {
	  nt_log_debug("Success for async IOCTL");
	}
	else {
	  nt_log_debug("Error for async IOCTL: %s (0x%x)",
		       nt_status_to_string(status), status);
	}
      }

      break;
    }

    case IRP_MJ_CLEANUP: {
      // we don't acquire the remove lock during cleanup
      _free_lock.cancel();

      NTSTATUS status = STATUS_SUCCESS;
      if (get_engaged(io_stack->FileObject)) {
	status = disengage_with_count();
      }
      irp->IoStatus.Status = status;
      break;
    }

    default: {
      irp->IoStatus.Status = STATUS_DRIVER_INTERNAL_ERROR;
      break;
    }
    }

    ASSERT(irp->IoStatus.Status != STATUS_PENDING);

    IoCompleteRequest(irp,
		      NT_SUCCESS(irp->IoStatus.Status) ?
		      IO_DISK_INCREMENT :
		      IO_NO_INCREMENT);
  }

  // the thread should not die without all file handles being
  // closed
  assert(!dcb->engage_count);
  assert(!reparse_handle);

  nt_log_debug("Thread dying, goodbyte!\n");
}

NTSTATUS
RAMDiskDevice::irp_create(PIRP irp) noexcept {
  PAGED_CODE();

  if (this->get_pnp_state() != PnPState::STARTED) {
    return standard_complete_irp(irp, STATUS_INVALID_DEVICE_STATE);
  }

  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff563633(v=vs.85).aspx
  auto io_stack = IoGetCurrentIrpStackLocation(irp);

  // Create always succeeds unless we are in the middle
  // of shutting down or a user tried to open a file
  // on the device, e.g. \\device\ramdisk\foo.txt
  auto status = io_stack->FileObject->FileName.Length
    ? STATUS_INVALID_PARAMETER
    : STATUS_SUCCESS;

  return standard_complete_irp(irp, status);
}

NTSTATUS
RAMDiskDevice::irp_close(PIRP irp) noexcept {
  PAGED_CODE();
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff563633(v=vs.85).aspx
  return standard_complete_irp(irp, STATUS_SUCCESS);
}

NTSTATUS
RAMDiskDevice::control_device_cleanup(PIRP irp) noexcept {
  IoMarkIrpPending(irp);
  this->queue_request(irp);
  return STATUS_PENDING;
}

NTSTATUS
RAMDiskDevice::_irp_read_or_write(PIRP irp) noexcept {
  if (this->get_pnp_state() != PnPState::STARTED) {
    return standard_complete_irp(irp, STATUS_INVALID_DEVICE_STATE);
  }

  RAMDiskDevice::RemoveLockGuard remove_lock_guard;
  auto status = this->create_remove_lock_guard(&remove_lock_guard);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error calling create_remove_lock_guard\n");
    return standard_complete_irp(irp, status);
  }

  auto io_stack = IoGetCurrentIrpStackLocation(irp);
  nt_log_debug("%s_irp\n",
	       io_stack->MajorFunction == IRP_MJ_READ ? "read" : "write");
  (void) io_stack;

  IoMarkIrpPending(irp);

  this->queue_request(irp);

  // cancel release of remove lock, this will happen in thread
  remove_lock_guard.cancel();

  return STATUS_PENDING;
}

NTSTATUS
RAMDiskDevice::irp_read(PIRP irp) noexcept {
  return _irp_read_or_write(irp);
}

NTSTATUS
RAMDiskDevice::irp_write(PIRP irp) noexcept {
  return _irp_read_or_write(irp);
}

NTSTATUS
RAMDiskDevice::irp_device_control(PIRP irp) noexcept {
  if (this->get_pnp_state() != PnPState::STARTED) {
    return standard_complete_irp(irp, STATUS_INVALID_DEVICE_STATE);
  }

  RAMDiskDevice::RemoveLockGuard remove_lock_guard;
  auto status0 = this->create_remove_lock_guard(&remove_lock_guard);
  if (!NT_SUCCESS(status0)) {
    nt_log_error("Error calling create_remove_lock_guard\n");
    return standard_complete_irp(irp, status0);
  }

  auto io_stack = IoGetCurrentIrpStackLocation(irp);
  auto ioctl = io_stack->Parameters.DeviceIoControl.IoControlCode;
  nt_log_debug("device_control_irp: IOCTL: %s (0x%x)\n",
	       ioctl_to_string(ioctl),
	       (unsigned) ioctl);

  NTSTATUS status = STATUS_UNSUCCESSFUL;
  switch (ioctl) {
  case IOCTL_DISK_CHECK_VERIFY: {
    status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    break;
  }

  case IOCTL_DISK_GET_DRIVE_GEOMETRY: {
    status = check_parameter_size(io_stack, sizeof(DISK_GEOMETRY));
    if (!NT_SUCCESS(status)) break;
    
    auto geometry = (PDISK_GEOMETRY) irp->AssociatedIrp.SystemBuffer;

    *geometry = this->geom;

    status = STATUS_SUCCESS;
    irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
    break;
  }

  case IOCTL_DISK_IS_WRITABLE: {
    status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    break;
  }

  case IOCTL_SAFE_RAMDISK_ENGAGE:
  case IOCTL_SAFE_RAMDISK_DISENGAGE: {
    IoMarkIrpPending(irp);

    this->queue_request(irp);

    // cancel release of remove lock, this will happen in thread
    remove_lock_guard.cancel();

    status = STATUS_PENDING;
    break;
  }

  default: {
    status = STATUS_INVALID_DEVICE_REQUEST;
    break;
  }
  }

  if (status != STATUS_PENDING) {
    if (NT_SUCCESS(status)) {
      nt_log_debug("Success for IOCTL");
    }
    else {
      nt_log_debug("Error for IOCTL: %s (0x%x)",
		   nt_status_to_string(status), status);
    }

    if (!NT_SUCCESS(status)) irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
  }
  else {
    nt_log_debug("Pending for IOCTL");
  }

  return status;
}

static
NTSTATUS
NTAPI
_pnp_start_device_completion_routine(PDEVICE_OBJECT,
				     PIRP irp,
				     PVOID ctx) {
  auto event = (PKEVENT) ctx;
  if (irp->PendingReturned) KeSetEvent(event, 0, FALSE);
  return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
RAMDiskDevice::irp_pnp(PIRP irp) {
  PAGED_CODE();

  RAMDiskDevice::RemoveLockGuard remove_lock_guard;
  auto status = this->create_remove_lock_guard(&remove_lock_guard);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error calling create_remove_lock_guard\n");
    return standard_complete_irp(irp, status);
  }

  auto io_stack = IoGetCurrentIrpStackLocation(irp);
  switch (io_stack->MinorFunction) {
  case IRP_MN_START_DEVICE: {
    KEVENT event;
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(irp, _pnp_start_device_completion_routine, 
			   (PVOID) &event, TRUE, TRUE, TRUE);
    auto status = IoCallDriver(this->lower_device_object, irp);
    if (status == STATUS_PENDING) {
      // We must wait for our PDO to successfully start the device
      // before we can mark ourselves as started
      KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
      status = irp->IoStatus.Status;
    }

    if (NT_SUCCESS(status)) this->set_pnp_state(PnPState::STARTED);

    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

  case IRP_MN_QUERY_STOP_DEVICE: {
    this->set_pnp_state(PnPState::STOP_PENDING);
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }

  case IRP_MN_CANCEL_STOP_DEVICE: {
    // If we actually received IRP_MN_QUERY_STOP_DEVICE first
    // then revert state
    if (this->get_pnp_state() == PnPState::STOP_PENDING) {
      this->set_pnp_state(PnPState::STARTED);
    }
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }

  case IRP_MN_STOP_DEVICE: {
    // We must release resources allocated during
    // IRP_MN_START_DEVICE on IRP_MN_STOP_DEVICE
    // since we allocate nothing on IRP_MN_START_DEVICE, do nothing
    this->set_pnp_state(PnPState::STOPPED);

    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }

  case IRP_MN_QUERY_REMOVE_DEVICE: {
    if (this->ramdisk_is_engaged()) {
      irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
    }

    this->set_pnp_state(PnPState::REMOVE_PENDING);
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }

  case IRP_MN_CANCEL_REMOVE_DEVICE: {
    // if state was set to REMOVE_PENDING by
    // IRP_MN_QUERY_REMOVE_DEVICE, reset it
    if (this->get_pnp_state() == PnPState::REMOVE_PENDING) {
      this->set_pnp_state(PnPState::STARTED);
    }
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }

  case IRP_MN_SURPRISE_REMOVAL: {
    // NB: we had no hardware resources associated with our
    // PDO so nothing to do here,
    // IRP_MN_REMOVE_DEVICE will still be called
    // (we will prevent new Create,DeviceControl,Read,Write IRPs though)
    this->set_pnp_state(PnPState::SURPRISE_REMOVE_PENDING);
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }

  case IRP_MN_REMOVE_DEVICE: {
    this->set_pnp_state(PnPState::DELETED);

    // we don't have to wait for the underlying driver's
    // completion for IRP_MN_REMOVE_DEVICE, we can just asynchronously
    // pass it down
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    auto status = IoCallDriver(this->lower_device_object, irp);

    // NB: we have to wait for all outstanding IRPs to finish 
    // http://msdn.microsoft.com/en-us/library/windows/hardware/ff565504%28v=vs.85%29.aspx
    remove_lock_guard.cancel();
    this->release_remove_lock_and_wait();

    // we would delete the device here, but we rely on our
    // caller to do that

    return status;
  }

  default: {
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(this->lower_device_object, irp);
  }
  }
}

NTSTATUS
RAMDiskDevice::irp_power(PIRP irp) {
  PAGED_CODE();
  PoStartNextPowerIrp(irp);
  IoSkipCurrentIrpStackLocation(irp);
  return PoCallDriver(this->lower_device_object, irp);
}

NTSTATUS
RAMDiskDevice::irp_system_control(PIRP irp) {
  PAGED_CODE();
  IoSkipCurrentIrpStackLocation(irp);
  return IoCallDriver(this->lower_device_object, irp);
}

static
RAMDiskDevice *
get_disk_control_block(PDEVICE_OBJECT device_object) noexcept {
  return static_cast<RAMDiskDevice *>(device_object->DeviceExtension);
}

void
delete_ramdisk_device(PDEVICE_OBJECT device_object) noexcept {
  nt_log_debug("deleting device!\n");
  auto dcb = get_disk_control_block(device_object);
  dcb->~RAMDiskDevice();
  IoDeleteDevice(device_object);
}

static
NTSTATUS
query_ramdisk_size(PDEVICE_OBJECT device_object,
                   PLARGE_INTEGER out);

NTSTATUS
create_ramdisk_device(PDRIVER_OBJECT driver_object,
		      PDEVICE_OBJECT physical_device_object) {
  static bool g_device_exists;

  nt_log_debug("creating new disk device!");

  if (g_device_exists) {
    nt_log_info("not creating a new device, one already exists");
    return STATUS_DEVICE_ALREADY_ATTACHED;
  }

  // TODO: We really should register device interface classes
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff549506%28v=vs.85%29.aspx
  // Also we should declare ourself as a DiskDevice device setup class
  // (but that requires implementing a lot of new IOCTLs)

  // Create Disk Device
  PDEVICE_OBJECT device_object;
  UNICODE_STRING device_name;
  RtlInitUnicodeString(&device_name, RAMDISK_DEVICE_NAME);
  auto status2 = IoCreateDevice(driver_object,
				sizeof(RAMDiskDevice),
				&device_name,
				FILE_DEVICE_DISK,
				FILE_DEVICE_SECURE_OPEN,
				FALSE,
				&device_object);
  if (!NT_SUCCESS(status2)) {
    nt_log_error("Error while calling IoCreateDevice: 0x%x\n",
		 (unsigned) status2);
    return status2;
  }
  auto _delete_device_object =
    lockbox::create_deferred(delete_ramdisk_device, device_object);

  device_object->Flags |= DO_POWER_PAGABLE;
  device_object->Flags |= DO_DIRECT_IO;

  LARGE_INTEGER ramdisk_size;
  auto status42 = query_ramdisk_size(physical_device_object,
                                     &ramdisk_size);
  if (!NT_SUCCESS(status42)) return status42;

  nt_log_debug("Making ramdisk with size: %llu",
               (ULONGLONG) ramdisk_size.QuadPart);

  // Attach our FDO to the device stack
  auto lower_device =
    IoAttachDeviceToDeviceStack(device_object, physical_device_object);
  if (!lower_device) {
    nt_log_error("Error while calling IoAttachDeviceToDeviceStack\n");
    return STATUS_NO_SUCH_DEVICE;
  }

  // Initialize our disk device runtime
  NTSTATUS status3;
  new (device_object->DeviceExtension) RAMDiskDevice(driver_object,
						     lower_device,
                                                     &ramdisk_size,
						     &status3);
  if (!NT_SUCCESS(status3)) {
    nt_log_error("Error while calling RAMDiskDevice.init: 0x%x\n",
		 (unsigned) status3);
    return status3;
  }

  // Cancel deferred delete device call since we succeeded
  _delete_device_object.cancel();
  device_object->Flags &= ~DO_DEVICE_INITIALIZING;

  g_device_exists = true;

  return STATUS_SUCCESS;
}

template <class T>
T
ceil_divide(T num, T denom) {
  return num / denom + !(num % denom);
}

static
NTSTATUS
format_fat32(HANDLE section, PLARGE_INTEGER memsize,
             PDISK_GEOMETRY pgeom,
	     PUCHAR ppartition_type) {
  const auto FAT_ID = 0xF8UL; // hard disk
  const auto BYTES_PER_SECTOR = 512UL;
  const auto SECTORS_PER_TRACK = 32UL;
  const auto TRACKS_PER_CYLINDER = 2UL;
  const auto NUM_RESERVED_SECTORS = 2UL;
  const auto SECTORS_PER_CLUSTER = 8UL; // 4K clusters
  const auto BYTES_PER_FAT_ENTRY = 4UL;

  // make sure we have room for at least one fat sector
  assert(memsize->QuadPart >=
	 (NUM_RESERVED_SECTORS + 1 +
	  SECTORS_PER_CLUSTER *
	  ceil_divide(BYTES_PER_SECTOR, BYTES_PER_FAT_ENTRY)) *
	 BYTES_PER_SECTOR);

  auto num_sectors_for_fat =
    (memsize->QuadPart / BYTES_PER_SECTOR - NUM_RESERVED_SECTORS) /
    (SECTORS_PER_CLUSTER *
     ceil_divide(BYTES_PER_SECTOR, BYTES_PER_FAT_ENTRY) +
     1);

  // initialize geometry
  pgeom->BytesPerSector = BYTES_PER_SECTOR;
  pgeom->SectorsPerTrack = SECTORS_PER_TRACK;
  pgeom->TracksPerCylinder = TRACKS_PER_CYLINDER;
  pgeom->Cylinders.QuadPart = (memsize->QuadPart / BYTES_PER_SECTOR /
			       SECTORS_PER_TRACK /
			       TRACKS_PER_CYLINDER);
  pgeom->MediaType = FixedMedia;

  static_assert(sizeof(uint8_t) == 1, "uint8_t is wrong size");
  static_assert(sizeof(uint16_t) == 2, "uint16_t is wrong size");
  static_assert(sizeof(uint32_t) == 4, "uint32_t is wrong size");

  // this is an on-disk structure
  {
#pragma pack(push, 1)
    struct {
      uint8_t jmp[3];
      uint8_t oem_name[8];
      // dos 2.0 BPB
      uint16_t bytes_per_sector;
      uint8_t sectors_per_cluster;
      uint16_t num_reserved_sectors;
      uint8_t num_fats;
      uint16_t num_root_directory_entries;
      uint16_t total_num_sectors;
      uint8_t media_descriptor;
      uint16_t sectors_per_fat;
      // dos 3.31 BPB
      uint16_t sectors_per_track;
      uint16_t num_heads;
      uint32_t hidden_sectors;
      uint32_t total_num_sectors_32;
      // fat32 bpb
      uint32_t sectors_per_fat_32;
      uint16_t mirroring_flags;
      uint16_t version;
      uint32_t root_directory_cluster_num;
      uint16_t fs_info_sector_num;
      uint16_t boot_sector_copy_sector_num;
      uint8_t _reserved1[12];
      // fat12/16 ebpb
      uint8_t drive_num;
      uint8_t _reserved2;
      uint8_t extended_boot_sig;
      uint32_t volume_id;
      uint8_t volume_label[11];
      uint8_t fs_type[8];
      // empty space
      uint8_t _reserved3[420];
      uint8_t sig[2];
  } boot_sector = {
      /*.jmp =*/ {0xeb, 0x76, 0x90},
      /*.oem_name =*/ {'S', 'A', 'F', 'E', 'R', 'A', 'M', 'D'},
      /*.bytes_per_sector =*/ BYTES_PER_SECTOR,
      /*.sectors_per_cluster =*/ SECTORS_PER_CLUSTER,
      /*.num_reserved_sectors =*/ NUM_RESERVED_SECTORS,
      /*.num_fats =*/ 1,
      /*.num_root_directory_entries =*/ 0,
      /*.total_num_sectors =*/ 0,
      /*.media_descriptor =*/ FAT_ID,
      /*.sectors_per_fat =*/ 0,
      /*.sectors_per_track =*/ SECTORS_PER_TRACK,
      /*.num_heads =*/ TRACKS_PER_CYLINDER,
      /*.hidden_sectors =*/ 0,
      /*.total_num_sectors_32 =*/ (uint32_t) (memsize->QuadPart / BYTES_PER_SECTOR),
      /*.sectors_per_fat_32 =*/ (uint32_t) num_sectors_for_fat,
      /*.mirroring_flags =*/ 0,
      /*.version =*/ 0,
      /*.root_directory_cluster_num =*/ 2,
      /*.fs_info_sector_num =*/ 1,
      /*.boot_sector_copy_sector_num =*/ 0,
      /*._reserved1[12] =*/ {0},
      /*.drive_num =*/ 0,
      /*._reserved2 =*/ 0,
      /*.extended_boot_sig =*/ 0x29,
      /*.volume_id =*/ 0x02051986,
      /*.volume_label[11] =*/ {'S', 'A', 'F', 'E',
			       'R', 'A', 'M',
			       'D', 'I', 'S', 'K'},
      /*.fs_type[8] =*/ {'F', 'A', 'T', '3', '2', ' ', ' ', ' '},
      /*._reserved3[420] =*/ {0},
      /*.sig[2] =*/ {0x55, 0xAA},
    };
#pragma pack(pop)

    static_assert(sizeof(boot_sector) == BYTES_PER_SECTOR,
		  "boot sector is too large");

    auto status = write_section(section, 0,
                                &boot_sector, BYTES_PER_SECTOR);
    if (!NT_SUCCESS(status)) return status;
  }

  {
#pragma pack(push, 1)
    struct {
      uint32_t signature_1;
      uint8_t _reserved1[480];
      uint32_t signature_2;
      uint32_t last_known_free_cluster_num;
      uint32_t most_recent_allocated_cluster_num;
      uint8_t _reserved2[12];
      uint32_t signature_3;
    } fs_info_sector = {
      /* signature_1 =*/ 0x41615252,
      /* _reserved1[480] =*/ {0},
      /* signature_2 =*/ 0x61417272,
      /* last_known_free_cluster_num =*/ 0xffffffff,
      /* most_recent_allocated_cluster_num =*/ 0xffffffff,
      /* _reserved2[12] =*/ {0},
      /* signature_3 =*/ 0xaa550000,
    };
#pragma pack(pop)

    static_assert(sizeof(fs_info_sector) == BYTES_PER_SECTOR,
		  "boot sector is too large");

    auto status = write_section(section, BYTES_PER_SECTOR,
                                &fs_info_sector, BYTES_PER_SECTOR);
    if (!NT_SUCCESS(status)) return status;
  }

  static_assert(!(FAT_ID >> 8), "FAT ID is larger than 1 bytes");
  uint32_t fat[3] = {
    (0xffffff00) | FAT_ID,
    0xffffffff,
    // this is the cluster where the root directory entry
    // starts, 0xffffffff means "last cluster in file"
    0xffffffff,
  };

  return write_section(section, BYTES_PER_SECTOR * 2,
                       fat, sizeof(fat));
}

template<class T, class U>
T
align_down(T addr, U gran) {
  return addr - addr % gran;
}

static
NTSTATUS
read_write_section(HANDLE section, size_t offset,
                   void *data, size_t to_transfer,
                   bool should_write) {
  const auto ALLOCATION_GRANULARITY = 64 * 1024;

  PUCHAR ramdisk_base_buffer = nullptr;

  // NB: must round down to align with ALLOCATION_GRANULARITY
  // (docs says this is done for us but that is a lie,
  //  if you don't manually round, it'll return
  //  STATUS_MAPPED_ALIGNMENT)

  LARGE_INTEGER section_offset;
  section_offset.QuadPart = align_down(offset, ALLOCATION_GRANULARITY);
  
  auto aligned_diff = offset - section_offset.QuadPart;
  SIZE_T to_transfer_copy = to_transfer + aligned_diff;

  auto status =
    ZwMapViewOfSection(section,
                       ZwCurrentProcess(),
                       (PVOID *) &ramdisk_base_buffer,
                       0, to_transfer_copy,
                       &section_offset,
                       &to_transfer_copy,
                       ViewUnmap, 0,
                       PAGE_READWRITE);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while doing ZwMapViewOfSection: %s (0x%x)",
                 nt_status_to_string(status), status);
    return status;
  }
    
  auto _unmap_view =
    lockbox::create_deferred(ZwUnmapViewOfSection,
                             ZwCurrentProcess(), ramdisk_base_buffer);

  auto ramdisk_buffer = ramdisk_base_buffer + aligned_diff;

  if (should_write) {
    memcpy(ramdisk_buffer, data, to_transfer);
  }
  else {
    memcpy(data, ramdisk_buffer, to_transfer);
  }

  return STATUS_SUCCESS;
}

static
NTSTATUS
read_section(HANDLE section, size_t offset,
             void *data, size_t to_transfer) {
  return read_write_section(section, offset,
                            const_cast<void *>(data), to_transfer,
                            false);
}

static
NTSTATUS
write_section(HANDLE section, size_t offset,
              const void *data, size_t to_transfer) {
  return read_write_section(section, offset,
                            const_cast<void *>(data), to_transfer,
                            true);
}

static
NTSTATUS
get_system_commit_limit(PLARGE_INTEGER out) {
  typedef struct {
    UCHAR Reserved1[4];
    ULONG MaximumIncrement;
    ULONG PhysicalPageSize;
    ULONG NumberOfPhysicalPages;
    ULONG LowestPhysicalPage;
    ULONG HighestPhysicalPage;
    ULONG AllocationGranularity;
    ULONG_PTR LowestUserAddress;
    ULONG_PTR HighestUserAddress;
    ULONG_PTR ActiveProcessors;
    CCHAR NumberOfProcessors;
  } MY_SYSTEM_BASIC_INFORMATION;

  typedef struct {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    ULONG ReadOperationCount;
    ULONG WriteOperationCount;
    ULONG OtherOperationCount;
    ULONG AvailablePages;
    ULONG TotalCommittedPages;
    ULONG TotalCommitLimit;
    ULONG PeakCommitment;
    ULONG PageFaults;
    ULONG WriteCopyFaults;
    ULONG TransitionFaults;
    ULONG CacheTransitionFaults;
    ULONG DemandZeroFaults;
    ULONG PagesRead;
    ULONG PageReadIos;
    ULONG CacheReads;
    ULONG CacheIos;
    ULONG PagefilePagesWritten;
    ULONG PagefilePageWriteIos;
    ULONG MappedFilePagesWritten;
    ULONG MappedFilePageWriteIos;
    ULONG PagedPoolUsage;
    ULONG NonPagedPoolUsage;
    ULONG PagedPoolAllocs;
    ULONG PagedPoolFrees;
    ULONG NonPagedPoolAllocs;
    ULONG NonPagedPoolFrees;
    ULONG TotalFreeSystemPtes;
    ULONG SystemCodePage;
    ULONG TotalSystemDriverPages;
    ULONG TotalSystemCodePages;
    ULONG SmallNonPagedLookasideListAllocateHits;
    ULONG SmallPagedLookasideListAllocateHits;
    ULONG Reserved3;
    ULONG MmSystemCachePage;
    ULONG PagedPoolPage;
    ULONG SystemDriverPage;
    ULONG FastReadNoWait;
    ULONG FastReadWait;
    ULONG FastReadResourceMiss;
    ULONG FastReadNotPossible;
    ULONG FastMdlReadNoWait;
    ULONG FastMdlReadWait;
    ULONG FastMdlReadResourceMiss;
    ULONG FastMdlReadNotPossible;
    ULONG MapDataNoWait;
    ULONG MapDataWait;
    ULONG MapDataNoWaitMiss;
    ULONG MapDataWaitMiss;
    ULONG PinMappedDataCount;
    ULONG PinReadNoWait;
    ULONG PinReadWait;
    ULONG PinReadNoWaitMiss;
    ULONG PinReadWaitMiss;
    ULONG CopyReadNoWait;
    ULONG CopyReadWait;
    ULONG CopyReadNoWaitMiss;
    ULONG CopyReadWaitMiss;
    ULONG MdlReadNoWait;
    ULONG MdlReadWait;
    ULONG MdlReadNoWaitMiss;
    ULONG MdlReadWaitMiss;
    ULONG ReadAheadIos;
    ULONG LazyWriteIos;
    ULONG LazyWritePages;
    ULONG DataFlushes;
    ULONG DataPages;
    ULONG ContextSwitches;
    ULONG FirstLevelTbFills;
    ULONG SecondLevelTbFills;
    ULONG SystemCalls;
  } MY_SYSTEM_PERFORMANCE_INFORMATION;

  typedef enum {
    MySystemBasicInformation = 0,
    MySystemPerformanceInformation = 2,
  } MY_SYSTEM_INFORMATION_CLASS;

  typedef NTSTATUS WINAPI
    (*ZwQuerySystemInformationType)(MY_SYSTEM_INFORMATION_CLASS,
				    PVOID,
				    ULONG,
				    PULONG);

  UNICODE_STRING fn_name;
  RtlInitUnicodeString(&fn_name, L"ZwQuerySystemInformation");

  auto func = (ZwQuerySystemInformationType)
    MmGetSystemRoutineAddress(&fn_name);
  if (!func) {
    nt_log_error("Couldn't find \"%wZ\" using MmGetSystemRoutineAddress",
                 &fn_name);
    return STATUS_PROCEDURE_NOT_FOUND;
  }

  // first get perf info
  MY_SYSTEM_PERFORMANCE_INFORMATION sys_perf_info;
  auto status = func(MySystemPerformanceInformation,
                     (PVOID) &sys_perf_info,
                     sizeof(sys_perf_info),
                     nullptr);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while doing perf ZwQuerySystemInformation: %s (0x%x)",
                 nt_status_to_string(status), status);
    return status;
  }

  // then get basic info
  MY_SYSTEM_BASIC_INFORMATION sys_basic_info;
  auto status2 = func(MySystemBasicInformation,
                      (PVOID) &sys_basic_info,
                      sizeof(sys_basic_info),
                      nullptr);
  if (!NT_SUCCESS(status2)) {
    nt_log_error("Error while doing basic ZwQuerySystemInformation: %s (0x%x)",
                 nt_status_to_string(status2), status2);
    return status2;
  }

  nt_log_debug("Queried commit limit: %llu, page_size: %llu\n",
               (long long unsigned) sys_perf_info.TotalCommitLimit,
               (long long unsigned) sys_basic_info.PhysicalPageSize);

  out->QuadPart = ((ULONGLONG) sys_perf_info.TotalCommitLimit *
                   (ULONGLONG) sys_basic_info.PhysicalPageSize);

  return STATUS_SUCCESS;
}

static
NTSTATUS
read_registry_alloc_size(PDEVICE_OBJECT device_object,
                         PLARGE_INTEGER out);

static
NTSTATUS
query_ramdisk_size(PDEVICE_OBJECT device_object,
                   PLARGE_INTEGER out) {
  // first see if we can find out an appropriate size using
  // ZwQuerySystemInformation
  auto status0 = get_system_commit_limit(out);
  if (NT_SUCCESS(status0)) {
    nt_log_debug("Queried ramdisk size from system commit limit: %llu",
                 (ULONGLONG) out->QuadPart);
    out->QuadPart /= 5ULL;
    return STATUS_SUCCESS;
  }

  // if not then try reading the registry
  auto status = read_registry_alloc_size(device_object, out);
  if (NT_SUCCESS(status)) {
    nt_log_debug("Queried ramdisk size from registry : %llu",
                 (ULONGLONG) out->QuadPart);
    return STATUS_SUCCESS;
  }

  // if all else fails, use our default value
  out->QuadPart = DEFAULT_MEM_SIZE;

  return STATUS_SUCCESS;
}

static
NTSTATUS
read_registry_alloc_size(PDEVICE_OBJECT pdo,
                         PLARGE_INTEGER out) {
  HANDLE reg_key;
  auto status = IoOpenDeviceRegistryKey(pdo, PLUGPLAY_REGKEY_DRIVER,
                                        KEY_READ, &reg_key);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while doing IoOpenDeviceRegistryKey: %s (0x%x)",
                 nt_status_to_string(status), status);
    return status;
  }

  auto _close_key = lockbox::create_deferred(ZwClose, reg_key);

  UNICODE_STRING value_name;
  RtlInitUnicodeString(&value_name, SAFE_RAMDISK_SIZE_VALUE_NAME_W);

  UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  auto value = (PKEY_VALUE_PARTIAL_INFORMATION) &buffer;
  ULONG value_length = sizeof(buffer);

  ULONG result_length;
  auto status1 = ZwQueryValueKey(reg_key,
                                 &value_name,
                                 KeyValuePartialInformation,
                                 value,
                                 value_length,
                                 &result_length);
  if (!NT_SUCCESS(status1)) {
    nt_log_error("Error while doing ZwQueryValueKey: %s (0x%x)",
                 nt_status_to_string(status1), status1);
    return status1;
  }
  
  if (value->Type != REG_DWORD) {
    nt_log_info("Bad registry type for RAMDiskSize");
    return STATUS_OBJECT_TYPE_MISMATCH;
  }

  out->QuadPart = *((PULONG) value->Data);
  
  return STATUS_SUCCESS;
}

}


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

#ifndef ZwCurrentProcess
#define ZwCurrentProcess() ((HANDLE)(LONG_PTR) -1)
#endif

const ULONG REMOVE_LOCK_TAG = 0x02051986UL;
const auto DISK_CONTROL_BLOCK_MAGIC = (uint32_t) 0x02051986UL;

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
format_fat32(HANDLE section, size_t memsize, PDISK_GEOMETRY pgeom,
	     PUCHAR ppartition_type);

static
NTSTATUS
read_registry_alloc_size(PUNICODE_STRING registry_path,
                         SIZE_T *out);

RAMDiskDevice::RAMDiskDevice(PDRIVER_OBJECT driver_object,
                             PUNICODE_STRING registry_path,
			     PDEVICE_OBJECT lower_device_object,
			     NTSTATUS *out) noexcept {
  this->thread_ref = nullptr;
  this->lower_device_object = lower_device_object;

  auto status2 = read_registry_alloc_size(registry_path,
                                          &this->image_size);
  if (!NT_SUCCESS(status2)) {
    *out = status2;
    return;
  }

  this->section_handle = nullptr;
  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     nullptr,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);
  LARGE_INTEGER section_size;
  section_size.QuadPart = (LONGLONG) this->image_size;
  auto status3 = ZwCreateSection(&this->section_handle,
                                 SECTION_ALL_ACCESS,
                                 &attributes,
                                 &section_size,
                                 PAGE_READWRITE,
                                 SEC_RESERVE,
                                 nullptr);
  if (!NT_SUCCESS(status3)) {
    nt_log_error("Error while doing ZwCreateSection: %s (0x%x)",
                 nt_status_to_string(status3), status3);
    *out = status3;
    return;
  }

  auto status42 = format_fat32(this->section_handle, this->image_size,
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

SIZE_T
RAMDiskDevice::get_image_size() const noexcept {
  return image_size;
}

void
RAMDiskDevice::queue_request(PIRP irp) noexcept {
  ExInterlockedInsertTailList(&this->list_head,
			      &irp->Tail.Overlay.ListEntry,
			      &this->list_lock);
  
  KeSetEvent(&this->request_event, (KPRIORITY) 0, FALSE);
}

PIRP
RAMDiskDevice::dequeue_request() noexcept {
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
				 NULL,
				 NULL);
      
      if (status == STATUS_WAIT_1) return nullptr;
      
      continue;
    }
    
    return CONTAINING_RECORD(request, IRP, Tail.Overlay.ListEntry);
  }
}

UCHAR
RAMDiskDevice::get_partition_type() const noexcept {
  return PARTITION_FAT32;
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

VOID
RAMDiskDevice::worker_thread() noexcept {
  auto dcb = this;

  nt_log_info("Worker thread, rock and roll!\n");

  auto engage_count = 0;
  HANDLE reparse_handle = nullptr;
  
  while (true) {
    auto irp = dcb->dequeue_request();

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
      
      assert(engage_count);

      if (engage_count == 1)  {
	assert(reparse_handle);
	status = disengage_reparse_point(reparse_handle);
	if (NT_SUCCESS(status)) reparse_handle = nullptr;
      }
	  
      if (NT_SUCCESS(status)) {
	engage_count -= 1;
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
	to_transfer = std::min((SIZE_T) io_stack->Parameters.Read.Length,
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
	  if (engage_count == 0) {
	    status = engage_reparse_point(&reparse_handle);
	  }
	  
	  if (NT_SUCCESS(status)) {
	    engage_count += 1;
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
  assert(!engage_count);
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

NTSTATUS
create_ramdisk_device(PDRIVER_OBJECT driver_object,
                      PUNICODE_STRING registry_path,
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
                                                     registry_path,
						     lower_device,
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
format_fat32(HANDLE section, size_t memsize, PDISK_GEOMETRY pgeom,
	     PUCHAR ppartition_type) {
  const auto FAT_ID = 0xF8UL; // hard disk
  const auto BYTES_PER_SECTOR = 512UL;
  const auto SECTORS_PER_TRACK = 32UL;
  const auto TRACKS_PER_CYLINDER = 2UL;
  const auto NUM_RESERVED_SECTORS = 2UL;
  const auto SECTORS_PER_CLUSTER = 8UL; // 4K clusters
  const auto BYTES_PER_FAT_ENTRY = 4UL;

  // make sure we have room for at least one fat sector
  assert(memsize >=
	 (NUM_RESERVED_SECTORS + 1 +
	  SECTORS_PER_CLUSTER *
	  ceil_divide(BYTES_PER_SECTOR, BYTES_PER_FAT_ENTRY)) *
	 BYTES_PER_SECTOR);

  auto num_sectors_for_fat =
    (memsize / BYTES_PER_SECTOR - NUM_RESERVED_SECTORS) /
    (SECTORS_PER_CLUSTER *
     ceil_divide(BYTES_PER_SECTOR, BYTES_PER_FAT_ENTRY) +
     1);

  // initialize geometry
  pgeom->BytesPerSector = BYTES_PER_SECTOR;
  pgeom->SectorsPerTrack = SECTORS_PER_TRACK;
  pgeom->TracksPerCylinder = TRACKS_PER_CYLINDER;
  pgeom->Cylinders.QuadPart = (memsize / BYTES_PER_SECTOR /
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
      /*.total_num_sectors_32 =*/ (uint32_t) (memsize / BYTES_PER_SECTOR),
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
                             section, ramdisk_base_buffer);

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
read_registry_alloc_size(PUNICODE_STRING registry_path,
                         SIZE_T *out) {
  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
                             registry_path,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                             NULL,
                             NULL);

  HANDLE reg_key = nullptr;
  auto status = ZwOpenKey(&reg_key, KEY_READ, &attributes);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while doing ZwOpenKey: %s (0x%x)",
                 nt_status_to_string(status), status);
    return status;
  }

  auto _close_key = lockbox::create_deferred(ZwClose, reg_key);

  UNICODE_STRING value_name;
  RtlInitUnicodeString(&value_name, L"RAMDiskSize");

  UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(DWORD)];
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
    if (status1 == STATUS_OBJECT_NAME_NOT_FOUND) {
      nt_log_info("Data is registry is bad, using default ramdisk size");
      *out = DEFAULT_MEM_SIZE;
      return STATUS_SUCCESS;
    }

    nt_log_error("Error while doing ZwQueryValueKey: %s (0x%x)",
                 nt_status_to_string(status1), status1);
    return status1;
  }
  
  if (value->Type != REG_DWORD) {
    *out = (SIZE_T) *((PDWORD) value->Data);
  }
  else {
    nt_log_info("Data is registry is bad, using default ramdisk size");
    *out = DEFAULT_MEM_SIZE;
  }

  return STATUS_SUCCESS;
}

}


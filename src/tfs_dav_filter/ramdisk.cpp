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

#include <tfs_dav_filter/ntoskrnl_cpp.hpp>

#include <lockbox/deferred.hpp>
#include <lockbox/low_util.hpp>

#include <ntdddisk.h>
#include <ntifs.h>

// define a macro for now,
// other options include using a variadic template
// or figuration out stdarg in windows kernel space
// these are better options for type-safety reasons
#define rd_log(...) (STATUS_SUCCESS == \
		     DbgPrint((char *) "RAMDISK: " __VA_ARGS__))
#define rd_log_debug rd_log
#define rd_log_info rd_log
#define rd_log_error rd_log

// our mingw header fixes
#define WaitAny 1

namespace rd {

enum class PnPState {
  NOT_STARTED,
  STARTED,
  STOP_PENDING,
  STOPPED,
  REMOVE_PENDING,
  SURPRISE_REMOVE_PENDING,
  DELETED,
};

// NB: this class must be trivially copyable (i.e. memcpy'able)
//     since it's constructed by the kernel runtime
class DiskControlBlock {
  static void _free_remove_lock(DiskControlBlock *dcb);

  typedef 
  decltype(lockbox::create_deferred(_free_remove_lock,
				    (DiskControlBlock *) nullptr))
  RemoveLockGuard;

  uint32_t magic;

  PDEVICE_OBJECT lower_device_object;

  void *image_buffer;
  SIZE_T image_size;
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

  SIZE_T
  get_image_size() const noexcept;

  void
  queue_request(PIRP irp) noexcept;

  PIRP
  dequeue_request() noexcept;

  UCHAR
  get_partition_type() const noexcept;

  VOID
  worker_thread() noexcept;

  NTSTATUS
  _irp_read_or_write(PIRP irp) noexcept;

  RemoveLockGuard
  create_remove_lock_guard();

  void
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

public:
  NTSTATUS
  init(PDEVICE_OBJECT lower_device_object) noexcept;

  void
  shutdown() noexcept;

  NTSTATUS
  irp_create(PIRP irp) noexcept;

  NTSTATUS
  irp_close(PIRP irp) noexcept;

  NTSTATUS
  irp_read(PIRP irp) noexcept;

  NTSTATUS
  irp_write(PIRP irp) noexcept;

  NTSTATUS
  irp_device_control(PIRP irp) noexcept;

  NTSTATUS
  irp_pnp(PIRP irp) noexcept;

  NTSTATUS
  irp_power(PIRP irp) noexcept;

  NTSTATUS
  irp_system_control(PIRP irp) noexcept;

  bool
  is_valid() noexcept;

  friend VOID NTAPI worker_thread_bootstrap(PVOID ctx) noexcept;
};

const size_t MEM_SIZE = 100 * 1024 * 1024;
const ULONG REMOVE_LOCK_TAG = 0x02051986UL;
const auto DISK_CONTROL_BLOCK_MAGIC = (uint32_t) 0x02051986UL;

static
const char *
format_ioctl(unsigned ioctl) {
#define __R(a) case a: return #a
  switch (ioctl) {
    __R(IOCTL_DISK_GET_DRIVE_GEOMETRY);
    __R(IOCTL_DISK_IS_WRITABLE);
    __R(IOCTL_DISK_GET_PARTITION_INFO_EX);
    __R(IOCTL_DISK_GET_LENGTH_INFO);
    __R(IOCTL_DISK_GET_DRIVE_GEOMETRY_EX);
    __R(IOCTL_DISK_CHECK_VERIFY);
    //    __R(IOCTL_DISK_MEDIA_REMOVAL);
    __R(IOCTL_DISK_GET_MEDIA_TYPES);
    __R(IOCTL_STORAGE_QUERY_PROPERTY);
    //    __R(IOCTL_STORAGE_GET_HOTPLUG_INFO);
    __R(IOCTL_STORAGE_GET_DEVICE_NUMBER);
    //__R(IOCTL_MOUNTDEV_QUERY_DEVICE_NAME);
    //__R(IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS);
    //__R(IOCTL_VOLUME_GET_GTP_ATTRIBUTES);
  default: return "unknown!";
  }
#undef __R
}

static
const char *
format_pnp_minor_function(unsigned minor) {
#define IRP_MN_DEVICE_ENUMERATED 0x19
#define __R(a) case a: return #a
  switch (minor) {
    __R(IRP_MN_START_DEVICE);
    __R(IRP_MN_QUERY_STOP_DEVICE);
    __R(IRP_MN_CANCEL_STOP_DEVICE);
    __R(IRP_MN_STOP_DEVICE);
    __R(IRP_MN_QUERY_REMOVE_DEVICE);
    __R(IRP_MN_CANCEL_REMOVE_DEVICE);
    __R(IRP_MN_SURPRISE_REMOVAL);
    __R(IRP_MN_REMOVE_DEVICE);
    __R(IRP_MN_QUERY_DEVICE_RELATIONS);
    __R(IRP_MN_FILTER_RESOURCE_REQUIREMENTS);
    __R(IRP_MN_DEVICE_ENUMERATED);
    default: return "unknown";
  }
#undef __R
}

static
const char *
format_nt_status(unsigned status) {
#define __R(a) case a: return #a
  switch (status) {
    __R(STATUS_SUCCESS);
    __R(STATUS_BUFFER_TOO_SMALL);
    __R(STATUS_WAIT_1);
    __R(STATUS_INSUFFICIENT_RESOURCES);
    __R(STATUS_DRIVER_INTERNAL_ERROR);
    __R(STATUS_PENDING);
    __R(STATUS_INVALID_DEVICE_STATE);
    __R(STATUS_INVALID_PARAMETER);
    __R(STATUS_UNSUCCESSFUL);
    __R(STATUS_MORE_PROCESSING_REQUIRED);
    __R(STATUS_NO_SUCH_DEVICE);
  default: return "unknown";
  }
#undef __R
}

static
void
format_fat32(void *mem, size_t memsize, PDISK_GEOMETRY pgeom,
	     PUCHAR ppartition_type);

static
NTSTATUS
standard_complete_irp(PIRP irp, NTSTATUS status) noexcept {
  irp->IoStatus.Status = status;
  irp->IoStatus.Information = 0;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  return status;
}

static
NTSTATUS
check_parameter_size(PIO_STACK_LOCATION io_stack, size_t s) {
  return (io_stack->Parameters.DeviceIoControl.OutputBufferLength < s) ?
    STATUS_BUFFER_TOO_SMALL :
    STATUS_SUCCESS;
}

void
DiskControlBlock::_free_remove_lock(DiskControlBlock *dcb) {
  dcb->release_remove_lock();
}

DiskControlBlock::RemoveLockGuard
DiskControlBlock::create_remove_lock_guard() {
  this->acquire_remove_lock();
  return lockbox::create_deferred(_free_remove_lock, this);
}

void
DiskControlBlock::acquire_remove_lock() {
  IoAcquireRemoveLock(&this->remove_lock, 0);
}

void
DiskControlBlock::release_remove_lock() {
  IoReleaseRemoveLock(&this->remove_lock, 0);
}

void
DiskControlBlock::release_remove_lock_and_wait() {
  IoReleaseRemoveLockAndWait(&this->remove_lock, 0);
}

VOID
NTAPI
worker_thread_bootstrap(PVOID ctx) noexcept {
  auto dcb = static_cast<DiskControlBlock *>(ctx);
  dcb->worker_thread();
}

bool
DiskControlBlock::is_valid() noexcept {
  return this->magic == DISK_CONTROL_BLOCK_MAGIC;
}

NTSTATUS
DiskControlBlock::init(PDEVICE_OBJECT lower_device_object) noexcept {
  this->magic = DISK_CONTROL_BLOCK_MAGIC;

  this->image_buffer = nullptr;
  this->thread_ref = nullptr;
  this->lower_device_object = lower_device_object;

  this->image_size = MEM_SIZE;
  auto status3 = ZwAllocateVirtualMemory(NtCurrentProcess(),
					 &this->image_buffer,
					 0,
					 &this->image_size,
					 MEM_COMMIT,
					 PAGE_READWRITE);
  if (!NT_SUCCESS(status3)) return status3;

  format_fat32(this->image_buffer, this->image_size,
	       &this->geom, &this->partition_type);

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
    rd_log_error("Error while calling PsCreateSystemThread: 0x%x\n",
	      (unsigned) status4);
    return status4;
  }
  ObReferenceObjectByHandle(thread_handle, (ACCESS_MASK) 0L,
			    NULL, KernelMode, &this->thread_ref,
			    NULL);
  ZwClose(thread_handle);

  return STATUS_SUCCESS;
}

void
DiskControlBlock::shutdown() noexcept {
  // signal for thread to die
  if (this->thread_ref) {
    rd_log_debug("Shutting down thread...\n");
    KeSetEvent(&this->terminate_thread, (KPRIORITY) 0, FALSE);

    // wait for thread to die
    auto status =
      KeWaitForSingleObject(this->thread_ref, Executive,
			    KernelMode, FALSE, NULL);
    if (STATUS_WAIT_0 != status) {
      rd_log_error("error while waiting on thread to die: 0x%x\n",
		   (unsigned) status);
    }
    ObDereferenceObject(this->thread_ref);
  }

  // free image buffer
  if (this->image_buffer) {
    SIZE_T free_size = 0;
    auto status = ZwFreeVirtualMemory(NtCurrentProcess(),
				      &this->image_buffer,
				      &free_size, MEM_RELEASE);
    if (!NT_SUCCESS(status)) {
      rd_log_error("couldn't free disk virtual memory: 0x%x\n",
		   (unsigned) status);
    }
  }

  if (this->lower_device_object) {
    IoDetachDevice(this->lower_device_object);
  }
}

SIZE_T
DiskControlBlock::get_image_size() const noexcept {
  return image_size;
}

void
DiskControlBlock::queue_request(PIRP irp) noexcept {
  // we explicitly use the Exf- prefix because some MinGW32 versions
  // incorrectly export ExInterlockedInsertTailList as STDCALL
  // when it's really a synonym for FASTCALL ExfInterlockedInsertTailList
  ExfInterlockedInsertTailList(&this->list_head,
			       &irp->Tail.Overlay.ListEntry,
			       &this->list_lock);
  
  KeSetEvent(&this->request_event, (KPRIORITY) 0, FALSE);
}

PIRP
DiskControlBlock::dequeue_request() noexcept {
  while (true) {
    auto request = ExfInterlockedRemoveHeadList(&this->list_head,
						&this->list_lock);
    if (!request) {
      // No request =>
      // wait for a request event or the terminate thread event
      PKEVENT wait_objects[] = {
	&this->request_event,
	&this->terminate_thread,
      };
      
      auto status =
	KeWaitForMultipleObjects(numelementsf(wait_objects),
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
DiskControlBlock::get_partition_type() const noexcept {
  return PARTITION_FAT32;
}
  
VOID
DiskControlBlock::worker_thread() noexcept {
  auto dcb = this;

  rd_log_info("Worker thread, rock and roll!\n");
  
  while (true) {
    auto irp = dcb->dequeue_request();

    // Got terminate message
    if (!irp) break;

    // queue a remove lock to be freed after this request is done
    auto _free_lock =
      lockbox::create_deferred(_free_remove_lock, this);

    // service request
    auto io_stack = IoGetCurrentIrpStackLocation(irp);
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
      if (io_stack->Parameters.Read.ByteOffset.QuadPart <
	  dcb->get_image_size()) {
	SIZE_T vm_offset = io_stack->Parameters.Read.ByteOffset.QuadPart;
	to_transfer = std::min(io_stack->Parameters.Read.Length,
			       dcb->get_image_size() - vm_offset);

	rd_log_debug("%s AT %lu -> %lu\n",
		  io_stack->MajorFunction == IRP_MJ_READ ? "READ" : "WRITE",
		  (unsigned long) vm_offset, (unsigned long) to_transfer);
	if (io_stack->MajorFunction == IRP_MJ_READ) {
	  RtlCopyMemory(system_buffer,
			static_cast<UINT8 *>(dcb->image_buffer) +
			vm_offset,
			to_transfer);
	}
	else {
	  RtlCopyMemory(static_cast<UINT8 *>(dcb->image_buffer) +
			vm_offset,
			system_buffer,
			to_transfer);
	}
      }
      else to_transfer = 0;

      if (to_transfer != io_stack->Parameters.Read.Length) {
	rd_log_debug("Short transfer, wanted: %lu, got %lu",
		  io_stack->Parameters.Read.Length, to_transfer);
      }
      
      irp->IoStatus.Status = STATUS_SUCCESS;
      irp->IoStatus.Information = to_transfer;

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

  rd_log_debug("Thread dying, goodbyte!\n");
}

NTSTATUS
DiskControlBlock::irp_create(PIRP irp) noexcept {
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
DiskControlBlock::irp_close(PIRP irp) noexcept {
  PAGED_CODE();
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff563633(v=vs.85).aspx
  return standard_complete_irp(irp, STATUS_SUCCESS);
}

NTSTATUS
DiskControlBlock::_irp_read_or_write(PIRP irp) noexcept {
  if (this->get_pnp_state() != PnPState::STARTED) {
    return standard_complete_irp(irp, STATUS_INVALID_DEVICE_STATE);
  }

  auto remove_lock_guard = this->create_remove_lock_guard();

  auto io_stack = IoGetCurrentIrpStackLocation(irp);
  rd_log_debug("%s_irp\n",
	       io_stack->MajorFunction == IRP_MJ_READ ? "read" : "write");

  IoMarkIrpPending(irp);

  this->queue_request(irp);

  // cancel release of remove lock, this will happen in thread
  remove_lock_guard.cancel();

  return STATUS_PENDING;
}

NTSTATUS
DiskControlBlock::irp_read(PIRP irp) noexcept {
  return _irp_read_or_write(irp);
}

NTSTATUS
DiskControlBlock::irp_write(PIRP irp) noexcept {
  return _irp_read_or_write(irp);
}

NTSTATUS
DiskControlBlock::irp_device_control(PIRP irp) noexcept {
  if (this->get_pnp_state() != PnPState::STARTED) {
    return standard_complete_irp(irp, STATUS_INVALID_DEVICE_STATE);
  }

  auto remove_lock_guard = this->create_remove_lock_guard();

  auto io_stack = IoGetCurrentIrpStackLocation(irp);
  auto ioctl = io_stack->Parameters.DeviceIoControl.IoControlCode;
  rd_log_debug("device_control_irp: IOCTL: %s (0x%x)\n",
	       format_ioctl(ioctl),
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

  default: {
    rd_log_debug("ERROR: IOCTL not supported!");
    status = STATUS_INVALID_DEVICE_REQUEST;
    break;
  }
  }

  ASSERT(status != STATUS_PENDING);

  if (!NT_SUCCESS(status)) irp->IoStatus.Information = 0;
  irp->IoStatus.Status = status;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
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
DiskControlBlock::irp_pnp(PIRP irp) {
  PAGED_CODE();

  auto remove_lock_guard = this->create_remove_lock_guard();

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

    // NB: we have to wait for all outstanding IRPs to finish 
    //     (since PnPState::STOPPED has been set we are guaranteed
    //      no new IRPs will commence)
    this->release_remove_lock_and_wait();
    this->acquire_remove_lock();

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

    // NB: we have to wait for all outstanding IRPs to finish 
    //     (since PnPState::DELETED has been set we are guaranteed
    //      no new IRPs will commence)
    this->release_remove_lock_and_wait();
    remove_lock_guard.cancel();

    // we don't have to wait for the underlying driver's
    // completion for IRP_MN_REMOVE_DEVICE, we can just asynchronously
    // pass it down
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    auto status = IoCallDriver(this->lower_device_object, irp);

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
DiskControlBlock::irp_power(PIRP irp) {
  PAGED_CODE();
  PoStartNextPowerIrp(irp);
  IoSkipCurrentIrpStackLocation(irp);
  return PoCallDriver(this->lower_device_object, irp);
}

NTSTATUS
DiskControlBlock::irp_system_control(PIRP irp) {
  PAGED_CODE();
  IoSkipCurrentIrpStackLocation(irp);
  return IoCallDriver(this->lower_device_object, irp);
}

static
DiskControlBlock *
get_disk_control_block(PDEVICE_OBJECT device_object) noexcept {
  return static_cast<DiskControlBlock *>(device_object->DeviceExtension);
}

static
void
delete_disk_device(PDEVICE_OBJECT device_object) noexcept {
  rd_log_debug("deleting device!\n");
  auto dcb = get_disk_control_block(device_object);
  dcb->shutdown();
  UNICODE_STRING sym_link;
  RtlInitUnicodeString(&sym_link, L"\\DosDevices\\G:");
  IoDeleteSymbolicLink(&sym_link);
  IoDeleteDevice(device_object);
}

static
NTSTATUS
NTAPI
create_disk_device(PDRIVER_OBJECT driver_object,
		   PDEVICE_OBJECT physical_device_object) {
  rd_log_debug("creating new disk device!");

  // Create Disk Device
  PDEVICE_OBJECT device_object;
  UNICODE_STRING device_name;
  RtlInitUnicodeString(&device_name, L"\\Device\\SafeRamDisk");
  auto status2 = IoCreateDevice(driver_object,
				sizeof(DiskControlBlock),
				&device_name,
				FILE_DEVICE_DISK,
				FILE_DEVICE_SECURE_OPEN,
				FALSE,
				&device_object);
  if (!NT_SUCCESS(status2)) {
    rd_log_error("Error while calling IoCreateDevice: 0x%x\n",
		 (unsigned) status2);
    return status2;
  }
  auto _delete_device_object =
    lockbox::create_deferred(delete_disk_device, device_object);

  device_object->Flags |= DO_POWER_PAGABLE;
  device_object->Flags |= DO_DIRECT_IO;

  // Attach our FDO to the device stack
  auto lower_device =
    IoAttachDeviceToDeviceStack(device_object, physical_device_object);
  if (!lower_device) {
    rd_log_error("Error while calling IoAttachDeviceToDeviceStack\n");
    return STATUS_NO_SUCH_DEVICE;
  }

  // Initialize our disk device runtime
  auto dcb = get_disk_control_block(device_object);
  auto status3 = dcb->init(lower_device);
  if (!NT_SUCCESS(status3)) {
    rd_log_error("Error while calling DiskControlBlock.init: 0x%x\n",
		 (unsigned) status3);
    return status3;
  }

  // Create device symlink
  UNICODE_STRING symbolic_link_name;
  RtlInitUnicodeString(&symbolic_link_name, L"\\DosDevices\\G:");
  auto status5 = IoCreateSymbolicLink(&symbolic_link_name, &device_name);
  if (!NT_SUCCESS(status5)) return status5;

  // Cancel deferred delete device call since we succeeded
  _delete_device_object.cancel();
  device_object->Flags &= ~DO_DEVICE_INITIALIZING;

  return STATUS_SUCCESS;
}

template <class T>
T
ceil_divide(T num, T denom) {
  return num / denom + !(num % denom);
}

static
void
format_fat32(void *mem, size_t memsize, PDISK_GEOMETRY pgeom,
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
      /*.total_num_sectors_32 =*/ memsize / BYTES_PER_SECTOR,
      /*.sectors_per_fat_32 =*/ num_sectors_for_fat,
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

    memcpy(mem, &boot_sector, BYTES_PER_SECTOR);
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

    memcpy((uint8_t *) mem + BYTES_PER_SECTOR, &fs_info_sector,
	   BYTES_PER_SECTOR);
  }

  const auto fat = (uint32_t *) ((uint8_t *) mem +
				 BYTES_PER_SECTOR * 2);
  static_assert(!(FAT_ID >> 8), "FAT ID is larger than 1 bytes");
  fat[0] = (0xffffff00) | FAT_ID;
  fat[1] = 0xffffffff;
  // this is the cluster where the root directory entry
  // starts, 0xffffffff means "last cluster in file"
  fat[2] = 0xffffffff;
}

}

template<NTSTATUS (rd::DiskControlBlock::*p)(PIRP)>
NTSTATUS
qd(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  auto dcb = rd::get_disk_control_block(DeviceObject);
  if (!dcb->is_valid()) return STATUS_INVALID_PARAMETER;
  return (dcb->*p)(Irp);
}

extern "C" {

static
NTSTATUS
NTAPI
SafeRamDiskDispatchCreate(PDEVICE_OBJECT DeviceObject,
			  PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_create>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchClose(PDEVICE_OBJECT DeviceObject,
			 PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_close>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchRead(PDEVICE_OBJECT DeviceObject,
			PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_read>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchWrite(PDEVICE_OBJECT DeviceObject,
			 PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_write>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchDeviceControl(PDEVICE_OBJECT DeviceObject,
				 PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_device_control>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchPnP(PDEVICE_OBJECT DeviceObject,
		       PIRP Irp) {
  // save minor function for dispatching pnp, stack location may become
  // invalidated by IoSkipCurrentIrpStackLocation
  auto minor_function = IoGetCurrentIrpStackLocation(Irp)->MinorFunction;
  auto status = qd<&rd::DiskControlBlock::irp_pnp>(DeviceObject, Irp);
  rd_log_debug("IRP_MJ_PNP %s (0x%x) -> %s (0x%x)",
	       rd::format_pnp_minor_function(minor_function),
	       minor_function,
	       rd::format_nt_status(status),
	       status);
  if (IRP_MN_REMOVE_DEVICE == minor_function) {
    rd::delete_disk_device(DeviceObject);
  }
  return status;
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchPower(PDEVICE_OBJECT DeviceObject,
			 PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_power>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskDispatchSystemControl(PDEVICE_OBJECT DeviceObject,
				 PIRP Irp) {
  return qd<&rd::DiskControlBlock::irp_system_control>(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
SafeRamDiskAddDevice(PDRIVER_OBJECT DriverObject, 
		     PDEVICE_OBJECT PhysicalDeviceObject) {
  return rd::create_disk_device(DriverObject, PhysicalDeviceObject);
}

static
void
NTAPI
SafeRamDiskUnload(PDRIVER_OBJECT DriverObject) { 
  // no-op since we allocate no resources associated with the driver
  // (IRP_MN_REMOVE_DEVICE will be called before this for each device
  //  we added)
  (void) DriverObject;
}

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath) {
  rd_log_info("Safe RAMDisk\n");
  rd_log_info("Built on %s %s\n", __DATE__, __TIME__);

  // Set up callbacks
  DriverObject->MajorFunction[IRP_MJ_CREATE] = SafeRamDiskDispatchCreate;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = SafeRamDiskDispatchClose;
  DriverObject->MajorFunction[IRP_MJ_READ] = SafeRamDiskDispatchRead;
  DriverObject->MajorFunction[IRP_MJ_WRITE] = SafeRamDiskDispatchWrite;
  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SafeRamDiskDispatchDeviceControl;
  DriverObject->MajorFunction[IRP_MJ_PNP] = SafeRamDiskDispatchPnP;
  DriverObject->MajorFunction[IRP_MJ_POWER] = SafeRamDiskDispatchPower;
  DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = SafeRamDiskDispatchSystemControl;
  DriverObject->DriverExtension->AddDevice = (PVOID) SafeRamDiskAddDevice;
  DriverObject->DriverUnload = SafeRamDiskUnload;

  rd_log_debug("Loading done, returning control...");

  // Return
  return STATUS_SUCCESS;
}

}

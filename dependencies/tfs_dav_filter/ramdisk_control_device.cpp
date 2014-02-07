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

#include "ramdisk_control_device.hpp"

#include "ntoskrnl_cpp.hpp"
#include "nt_helpers.hpp"
#include "ramdisk_ioctl.h"

#include <lockbox/deferred.hpp>

#include <ntifs.h>

#ifndef OBJ_KERNEL_HANDLE
#define OBJ_KERNEL_HANDLE       0x00000200
#endif

// NB: some versions of MinGW32 screws this up
#define MY_FSCTL_SET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 41, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

namespace safe_nt {

const ULONG QUERY_VALUE_KEY_ALLOCATE_TAG = 0x20202020;
const ULONG GET_PATH_TO_TFS_DAV_TAG = 0x20202021;
const ULONG RENAME_TFS_DAV_DIRECTORY_TAG = 0x20202022;
const ULONG FREE_UNICODE_STRING_TAG = 0x20202023;

template <class T>
void *
add_ptr(T *a, size_t amt) {
  return (void *) (((uint8_t *)a) + amt);
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
NTSTATUS
query_value_key_allocate(HANDLE KeyHandle,
			 PUNICODE_STRING ValueName,
			 KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
			 PVOID *KeyValueInformation,
			 PULONG ResultLength,
			 POOL_TYPE PoolType) {
  ULONG value_length;

  // Get the length of the registry key value
  auto status = ZwQueryValueKey(KeyHandle,
				ValueName,
				KeyValueInformationClass,
				NULL,
				0,
				&value_length);
  if (status != STATUS_BUFFER_TOO_SMALL && status != STATUS_BUFFER_OVERFLOW) {
    nt_log_error("Error while calling ZwQueryValueKey(\"%wZ\"): %s (0x%x)",
		 ValueName, nt_status_to_string(status), status);
    return STATUS_INVALID_PARAMETER;
  }

  // Allocate enough space for the key value
  *KeyValueInformation = ExAllocatePoolWithTag(PoolType, value_length,
					       QUERY_VALUE_KEY_ALLOCATE_TAG);
  if (!*KeyValueInformation) {
    nt_log_error("Error while calling ExAllocatePoolWithTag");
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  auto _free_key_value_information =
    lockbox::create_deferred(ExFreePool, *KeyValueInformation);

  auto status2 = ZwQueryValueKey(KeyHandle,
				 ValueName,
				 KeyValueInformationClass,
				 *KeyValueInformation,
				 value_length,
				 ResultLength);
  if (!NT_SUCCESS(status2)) {
    nt_log_error("Error while calling ZwQueryValueKey(\"%wZ\"): %s (0x%x)",
		 ValueName, nt_status_to_string(status2), status2);
    return status2;
  }

  _free_key_value_information.cancel();

  return STATUS_SUCCESS;
}

static
NTSTATUS
get_path_to_tfs_dav(PUNICODE_STRING path_to_tfs_dav) {
  // How to use the registry from a WDM driver:
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff565537%28v=vs.85%29.aspx

  // open reg key that contains path to sys folder
  UNICODE_STRING registry_path;
  RtlInitUnicodeString(&registry_path,
		       L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     &registry_path,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);

  HANDLE windows_reg_key;
  auto status = ZwOpenKey(&windows_reg_key, KEY_READ, &attributes);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while calling ZwOpenKey(\"%wZ\"): %s (0x%x)",
		 &registry_path, nt_status_to_string(status), status);
    return status;
  }
  auto _close_key =
    lockbox::create_deferred(ZwClose, windows_reg_key);

  UNICODE_STRING value_name;
  RtlInitUnicodeString(&value_name, L"SystemRoot");

  PKEY_VALUE_PARTIAL_INFORMATION key_value;
  ULONG key_value_length;
  auto status2 = query_value_key_allocate(windows_reg_key,
					  &value_name,
					  KeyValuePartialInformation,
					  (PVOID *) &key_value,
					  &key_value_length,
					  PagedPool);
  if (!NT_SUCCESS(status2)) return status2;
  auto _free_key_value =
    lockbox::create_deferred(ExFreePool, key_value);

  // okay we have something like 'C:\Windows' now
  // just append \\ServiceProfiles\\ etc.

  // NB: must prepend C:\ with \??\ since C: refers to a Win32 namespace
  UNICODE_STRING leading_string;
  RtlInitUnicodeString(&leading_string, L"\\??\\");

  UNICODE_STRING trailing_string;
  RtlInitUnicodeString(&trailing_string,
		       L"\\ServiceProfiles\\LocalService\\AppData\\Local\\Temp\\TfsStore\\Tfs_DAV");

  auto total_length =
    leading_string.Length
    // NB: -sizeof(WCHAR) because key_value->Data contains trailing NULL
    + key_value->DataLength - sizeof(WCHAR)
    + trailing_string.Length;

  auto buffer = ExAllocatePoolWithTag(PagedPool, total_length,
				      GET_PATH_TO_TFS_DAV_TAG);
  if (!buffer) {
    nt_log_error("Error while calling ExAllocatePoolWithTag(%u, 0x%x)",
		 (unsigned) total_length, (unsigned) GET_PATH_TO_TFS_DAV_TAG);
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  path_to_tfs_dav->Buffer = (PWSTR) buffer;
  path_to_tfs_dav->Length = 0;
  path_to_tfs_dav->MaximumLength = total_length;

  RtlCopyUnicodeString(path_to_tfs_dav, &leading_string);

  memcpy(add_ptr(path_to_tfs_dav->Buffer, path_to_tfs_dav->Length),
	 key_value->Data,
	 key_value->DataLength - sizeof(WCHAR));
  path_to_tfs_dav->Length += key_value->DataLength - sizeof(WCHAR);

  RtlAppendUnicodeStringToString(path_to_tfs_dav, &trailing_string);

  return STATUS_SUCCESS;
}

static
NTSTATUS
does_tfs_dav_link_already_exists(PUNICODE_STRING path_to_tfs_dav,
				 PHANDLE reparse_handle) {
  *reparse_handle = nullptr;

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     path_to_tfs_dav,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);

  HANDLE existing_tfs_dav_handle;
  IO_STATUS_BLOCK io_status_block;
  auto status2 = ZwOpenFile(&existing_tfs_dav_handle,
			    DELETE | SYNCHRONIZE,
			    &attributes,
			    &io_status_block,
			    FILE_SHARE_READ
			    | FILE_SHARE_WRITE
			    | FILE_SHARE_DELETE,
			    FILE_DIRECTORY_FILE
			    | FILE_OPEN_FOR_BACKUP_INTENT
			    | FILE_SYNCHRONOUS_IO_ALERT
			    | FILE_OPEN_REPARSE_POINT
			    | FILE_DELETE_ON_CLOSE);
  if (!NT_SUCCESS(status2) &&
      io_status_block.Information != FILE_DOES_NOT_EXIST) {
    nt_log_error("Error while calling ZwOpenFile(\"%wZ\"): %s (0x%x)",
		 path_to_tfs_dav,
		 nt_status_to_string(status2), status2);
    return status2;
  }

  if (io_status_block.Information == FILE_DOES_NOT_EXIST) {
    return STATUS_SUCCESS;
  }

  auto _close_file = lockbox::create_deferred(ZwClose, existing_tfs_dav_handle);

  // read link
  uint8_t reparse_data_buffer[sizeof(REPARSE_DATA_BUFFER) + 1024];
  IO_STATUS_BLOCK io_status_block_2;
  auto status3 = ZwFsControlFile(existing_tfs_dav_handle,
				 nullptr,
				 nullptr,
				 nullptr,
				 &io_status_block_2,
				 FSCTL_GET_REPARSE_POINT,
				 nullptr,
				 0,
				 reparse_data_buffer,
				 sizeof(reparse_data_buffer));
  if (!NT_SUCCESS(status3)) {
    // NB: if it's not a reparse point, this isn't an error
    if (status3 == STATUS_NOT_A_REPARSE_POINT) {
      return STATUS_SUCCESS;
    }

    nt_log_error("Error while calling ZwFsControlFile: %s (0x%x)",
		 nt_status_to_string(status3), status3);
    return status3;
  }

  auto reparse_data = (PREPARSE_DATA_BUFFER) reparse_data_buffer;

  UNICODE_STRING reparse_point_target;
  reparse_point_target.Buffer =
    &reparse_data->MountPointReparseBuffer.PathBuffer[reparse_data->MountPointReparseBuffer.SubstituteNameOffset
						      / sizeof(reparse_data->MountPointReparseBuffer.PathBuffer[0])];
  reparse_point_target.Length = reparse_data->MountPointReparseBuffer.SubstituteNameLength;
  reparse_point_target.MaximumLength = reparse_point_target.Length;

  UNICODE_STRING intended_target;
  RtlInitUnicodeString(&intended_target, L"\\??\\G:\\");

  if (RtlEqualUnicodeString(&intended_target, &reparse_point_target, TRUE)) {
    *reparse_handle = existing_tfs_dav_handle;
    _close_file.cancel();
  }

  return STATUS_SUCCESS;
}

enum class RenameDirection {
  FORWARDS,
  BACKWARDS,
};

static
NTSTATUS
rename_tfs_dav_directory(PUNICODE_STRING path_to_tfs_dav, RenameDirection dir) {
  // create new path
  UNICODE_STRING new_tfs_dav_path;
  {
    UNICODE_STRING new_tfs_dav_path_suffix;
    RtlInitUnicodeString(&new_tfs_dav_path_suffix, L"-SafeBackup");


    new_tfs_dav_path.MaximumLength = (new_tfs_dav_path_suffix.Length
				      + path_to_tfs_dav->Length);
    new_tfs_dav_path.Length = 0;
    new_tfs_dav_path.Buffer =
      (PWSTR) ExAllocatePoolWithTag(PagedPool,
				    new_tfs_dav_path.MaximumLength,
				    RENAME_TFS_DAV_DIRECTORY_TAG);
    if (!new_tfs_dav_path.Buffer) {
      nt_log_error("Error while calling ExAllocatePoolWithTag(%u, 0x%x)",
		   (unsigned) new_tfs_dav_path.MaximumLength,
		   (unsigned) RENAME_TFS_DAV_DIRECTORY_TAG);
      return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&new_tfs_dav_path, path_to_tfs_dav);
    RtlAppendUnicodeStringToString(&new_tfs_dav_path, &new_tfs_dav_path_suffix);
  }

  PUNICODE_STRING source_path, dest_path;

  if (dir == RenameDirection::FORWARDS) {
    source_path = path_to_tfs_dav;
    dest_path = &new_tfs_dav_path;
  }
  else {
    source_path = &new_tfs_dav_path;
    dest_path = path_to_tfs_dav;
  }

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     source_path,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);

  HANDLE existing_tfs_dav_handle;
  IO_STATUS_BLOCK io_status_block;
  auto status2 = ZwOpenFile(&existing_tfs_dav_handle,
			    DELETE | SYNCHRONIZE,
			    &attributes,
			    &io_status_block,
			    FILE_SHARE_READ
			    | FILE_SHARE_WRITE
			    | FILE_SHARE_DELETE,
			    FILE_DIRECTORY_FILE
			    | FILE_SYNCHRONOUS_IO_ALERT
			    | FILE_OPEN_FOR_BACKUP_INTENT
			    | FILE_OPEN_REPARSE_POINT);
  if (!NT_SUCCESS(status2) &&
      io_status_block.Information == FILE_DOES_NOT_EXIST) {
    nt_log_error("Error while calling ZwOpenFile(\"%wZ\"): %s (0x%x)",
		 source_path, nt_status_to_string(status2), status2);
    return status2;
  }

  // file doesn't exist, no need to do anything
  if (io_status_block.Information == FILE_DOES_NOT_EXIST) return STATUS_SUCCESS;

  auto _close_file = lockbox::create_deferred(ZwClose, existing_tfs_dav_handle);


  // issue rename
  {
    uint8_t rename_information_buffer[sizeof(FILE_RENAME_INFORMATION)
				      + dest_path->Length];
    auto rename_information =
      (PFILE_RENAME_INFORMATION) rename_information_buffer;

    // UH OH PTR ALIASING
    rename_information->ReplaceIfExists = FALSE;
    rename_information->RootDirectory = NULL;
    rename_information->FileNameLength = dest_path->Length;
    memcpy(rename_information->FileName,
	   dest_path->Buffer,
	   dest_path->Length);

    IO_STATUS_BLOCK io_status_block_2;
    auto status3 = ZwSetInformationFile(existing_tfs_dav_handle,
					&io_status_block_2,
					(PVOID) rename_information,
					sizeof(rename_information_buffer),
					FileRenameInformation);
    if (!NT_SUCCESS(status3)) {
      nt_log_error("Error while calling ZwSetInformationFile: %s (0x%x)",
		   nt_status_to_string(status3), status3);
      return status3;
    }
  }

  return STATUS_SUCCESS;
}

static
NTSTATUS
create_new_tfs_dav_link(PUNICODE_STRING path_to_tfs_dav, PHANDLE out_handle) {
  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     path_to_tfs_dav,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);

  IO_STATUS_BLOCK io_status_block;
  HANDLE tfs_dav_handle;
  auto status = ZwCreateFile(&tfs_dav_handle,
			     DELETE | GENERIC_ALL | SYNCHRONIZE,
			     &attributes,
			     &io_status_block,
			     nullptr,
			     FILE_ATTRIBUTE_NORMAL,
			     0,
			     FILE_OPEN_IF,
			     FILE_DIRECTORY_FILE
			     | FILE_OPEN_REPARSE_POINT
			     | FILE_OPEN_FOR_BACKUP_INTENT
			     | FILE_SYNCHRONOUS_IO_NONALERT
			     | FILE_DELETE_ON_CLOSE,
			     NULL, 0);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while calling ZwCreateFile: %s (0x%x)",
		 nt_status_to_string(status), status);
    return status;
  }

  auto _close_file = lockbox::create_deferred(ZwClose, tfs_dav_handle);

  UNICODE_STRING reparse_point_target;
  RtlInitUnicodeString(&reparse_point_target, L"\\??\\G:\\");

  const size_t total_size =
    8 + 8 + reparse_point_target.Length + 2 * sizeof(WCHAR);
  uint8_t reparse_data_buffer[total_size];
  memset(reparse_data_buffer, 0, sizeof(reparse_data_buffer));
  auto reparse_data = (PREPARSE_DATA_BUFFER) reparse_data_buffer;

  reparse_data->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  // NB: not exactly sure why there should be 2 extra characters
  //     at the end, my best guess is that we pretend the reparse
  //     point is null-terminated and have a place for PrintNameOffset
  //     to start (even though its length = 0)
  reparse_data->ReparseDataLength = (8 + reparse_point_target.Length
				     + 2 * sizeof(WCHAR));
  reparse_data->MountPointReparseBuffer.SubstituteNameLength =
    reparse_point_target.Length;
  reparse_data->MountPointReparseBuffer.PrintNameOffset =
    reparse_point_target.Length + sizeof(WCHAR);

  memcpy(reparse_data->MountPointReparseBuffer.PathBuffer,
	 reparse_point_target.Buffer,
	 reparse_point_target.Length);
  
  IO_STATUS_BLOCK io_status_block_2;
  auto status2 = ZwFsControlFile(tfs_dav_handle,
				 nullptr,
				 nullptr,
				 nullptr,
				 &io_status_block_2,
				 MY_FSCTL_SET_REPARSE_POINT,
				 reparse_data_buffer,
				 sizeof(reparse_data_buffer),
				 nullptr,
				 0);
  if (!NT_SUCCESS(status2)) {
    nt_log_error("Error while calling ZwFsControlFile(\"%wZ\"): %s (0x%x)",
		 path_to_tfs_dav,
		 nt_status_to_string(status2), status2);
    return status2;
  }

  *out_handle = tfs_dav_handle;

  _close_file.cancel();

  return STATUS_SUCCESS;
}

static
NTSTATUS
free_unicode_string(PUNICODE_STRING us) {
  ExFreePoolWithTag(us->Buffer, FREE_UNICODE_STRING_TAG);
  return STATUS_SUCCESS;
}

RAMDiskControlDevice::RAMDiskControlDevice(NTSTATUS *out) noexcept
  : _engage_count(0)
  , _reparse_handle(nullptr) {
  KeInitializeMutex(&_engage_mutex, 0);
  *out = STATUS_SUCCESS;
}

RAMDiskControlDevice::~RAMDiskControlDevice() noexcept {
  // we should not be destructing the device if there are handles open
  // and if there aren't any handles open we shouldn't be engaged
  assert(!_engage_count);
  assert(!_reparse_handle);
}

NTSTATUS
RAMDiskControlDevice::_create_engage_lock_guard(RAMDiskControlDevice::EngageLockGuard *out) {
  // NB: we allow both User APCs (Alertable = TRUE) and
  //     thread termination signals (WaitMode = UserMode) since we are
  //     running in the context of the user thread that called us
  auto status = KeWaitForMutexObject(&_engage_mutex, Executive,
				     UserMode, TRUE, nullptr);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while calling KeWaitForMutexObject: %s (0x%x)",
		 nt_status_to_string(status), status);
    return status;
  }

  *out = lockbox::create_deferred(KeReleaseMutex, &_engage_mutex, FALSE);

  return STATUS_SUCCESS;
}

NTSTATUS
RAMDiskControlDevice::_engage() {
  PAGED_CODE();

  // Acquire engage mutex
  RAMDiskControlDevice::EngageLockGuard engage_lock_guard;
  auto status = _create_engage_lock_guard(&engage_lock_guard);
  if (!NT_SUCCESS(status)) return status;

  // check engage count, if not zero then just increment
  if (_engage_count) {
    nt_log_debug("RAM disk was already engaged, incrementing engage count");
    _engage_count += 1;
    return STATUS_SUCCESS;
  }

  nt_log_info("Engage count is going to non-zero, engaging reparse point...");

  // get path to Tfs_DAV
  UNICODE_STRING path_to_tfs_dav;
  auto status1 = get_path_to_tfs_dav(&path_to_tfs_dav);
  if (!NT_SUCCESS(status1)) return status1;

  auto _free_tfs_path =
    lockbox::create_deferred(free_unicode_string, &path_to_tfs_dav);

  // check if link already exists
  auto status2 = does_tfs_dav_link_already_exists(&path_to_tfs_dav,
						  &_reparse_handle);
  if (!NT_SUCCESS(status2)) return status2;

  if (!_reparse_handle) {
    // rename old entry if it exists
    auto status3 = rename_tfs_dav_directory(&path_to_tfs_dav,
					    RenameDirection::FORWARDS);
    if (!NT_SUCCESS(status3)) {
      if (status3 == STATUS_OBJECT_NAME_COLLISION) {
	nt_log_info("Backup directory already exists, leaving it...");
      }
      else if (status3 == STATUS_OBJECT_PATH_NOT_FOUND) {
	nt_log_info("No original Tfs_DAV directory existed, "
		    "so no backup could be made");
      }
      else return status3;
    }
    
    // create new link
    auto status4 = create_new_tfs_dav_link(&path_to_tfs_dav, &_reparse_handle);
    if (!NT_SUCCESS(status4)) return status4;
  }
  else nt_log_debug("RAM disk reparse point already existed...");
    
  assert(_reparse_handle);
  _engage_count += 1;

  nt_log_info("Engage complete");

  return STATUS_SUCCESS;
}

NTSTATUS
RAMDiskControlDevice::_disengage() {
  PAGED_CODE();

  // Acquire engage mutex
  RAMDiskControlDevice::EngageLockGuard engage_lock_guard;
  auto status0 = _create_engage_lock_guard(&engage_lock_guard);
  if (!NT_SUCCESS(status0)) return status0;

  if (!_engage_count) return STATUS_INVALID_PARAMETER;

  assert(_reparse_handle);

  // there are still people engaged, just return after decrementing
  if (_engage_count > 1) {
    nt_log_debug("Engage count was >1, just decrementing instead of disengaging");
    _engage_count -= 1;
    return STATUS_SUCCESS;
  }

  nt_log_info("Engage count is going to zero, disengaging reparse point...");

  // close handle to dav link
  auto status = ZwClose(_reparse_handle);
  if (!NT_SUCCESS(status)) {
    // NB: if we fail to close the handle, we won't be able to
    //     delete/move the directory and restore the old directory
    nt_log_error("Failure to close reparse handle, leaking...");
    return status;
  }

  _reparse_handle = nullptr;

  // NB: at this point,
  //     the reparse point has been closed and deleted since it was opened
  //     with FILE_DELETE_ON_CLOSE

  // decrement the engage count
  _engage_count -= 1;

  // NB: we no long consider ourselves engaged
  //     if we fail in any of the following operations _engage will
  //     need to handle the bad state to recover
  do {
    // get path to tfs dav directory
    UNICODE_STRING path_to_tfs_dav;
    auto status1 = get_path_to_tfs_dav(&path_to_tfs_dav);
    if (!NT_SUCCESS(status1)) {
      nt_log_error("get_path_to_tfs_dav() failed, cannot move backup directory "
		   "to original location");
      break;
    }

    auto _free_tfs_path =
      lockbox::create_deferred(free_unicode_string, &path_to_tfs_dav);

    // rename backup directory to old location
    auto status2 = rename_tfs_dav_directory(&path_to_tfs_dav,
					    RenameDirection::BACKWARDS);
    if (!NT_SUCCESS(status2)) {
      if (status2 == STATUS_OBJECT_PATH_NOT_FOUND) {
	// backup didn't exist, oh well
      }
      else {
	nt_log_error("rename_tfs_dav_directory() failed, backup directory "
		     "was not moved back to original location");
      }
      break;
    }
  }
  while (false);

  nt_log_info("Disengage complete");

  return STATUS_SUCCESS;
}

NTSTATUS
RAMDiskControlDevice::irp_create(PIRP irp) {
  nt_log_debug("RAMDiskControlDevice::irp_create called");

  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff563633(v=vs.85).aspx
  auto io_stack = IoGetCurrentIrpStackLocation(irp);

  // Create always succeeds unless we are in the middle
  // of shutting down or a user tried to open a file
  // on the device, e.g. \\device\ramdisk\foo.txt
  auto status = io_stack->FileObject->FileName.Length
    ? STATUS_INVALID_PARAMETER
    : STATUS_SUCCESS;

  set_engaged(io_stack->FileObject, false);

  return standard_complete_irp(irp, status);
}

NTSTATUS
RAMDiskControlDevice::irp_cleanup(PIRP irp) {
  nt_log_debug("RAMDiskControlDevice::irp_cleanup called");

  auto io_stack = IoGetCurrentIrpStackLocation(irp);

  NTSTATUS status;
  if (get_engaged(io_stack->FileObject)) {
    status = _disengage();
    if (NT_SUCCESS(status)) {
      set_engaged(io_stack->FileObject, false);
    }
  }
  else {
    status = STATUS_SUCCESS;
  }

  return standard_complete_irp(irp, status);
}

NTSTATUS
RAMDiskControlDevice::irp_close(PIRP irp) {
  nt_log_debug("RAMDiskControlDevice::irp_close called");

  return standard_complete_irp(irp, STATUS_SUCCESS);
}

NTSTATUS
RAMDiskControlDevice::irp_device_control(PIRP irp) {
  nt_log_debug("RAMDiskControlDevice::irp_device_control called");

  auto io_stack = IoGetCurrentIrpStackLocation(irp);

  NTSTATUS status;
  switch (io_stack->Parameters.DeviceIoControl.IoControlCode) {
  case IOCTL_SAFE_RAMDISK_ENGAGE: {
    status = _engage();
    if (NT_SUCCESS(status)) set_engaged(io_stack->FileObject, true);
    else nt_log_error("Calling _engage failed");
    break;
  }
  case IOCTL_SAFE_RAMDISK_DISENGAGE: {
    status = _disengage();
    if (NT_SUCCESS(status)) set_engaged(io_stack->FileObject, false);
    else  nt_log_error("Calling _disengage failed");
    break;
  }
  default: {
    status = STATUS_INVALID_DEVICE_REQUEST;
    break;
  }
  }

  return standard_complete_irp(irp, status);
}

NTSTATUS
create_control_device(PDRIVER_OBJECT driver_object,
		      PDEVICE_OBJECT ramdisk_device,
		      PDEVICE_OBJECT *out) {
  UNICODE_STRING ctl_device_name;
  RtlInitUnicodeString(&ctl_device_name, RAMDISK_CTL_DEVICE_NAME_W);

  PDEVICE_OBJECT device_object;
  auto status = IoCreateDevice(driver_object,
			       sizeof(RAMDiskControlDevice),
			       &ctl_device_name,
			       FILE_DEVICE_SAFE_RAMDISK_CTL,
			       0,
			       FALSE,
			       &device_object);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while doing IoCreateDevice: %s (0x%x)",
		 nt_status_to_string(status),
		 status);
    return status;
  }
  auto _delete_device_object =
    lockbox::create_deferred(delete_control_device, device_object);

  NTSTATUS status3;
  new (device_object->DeviceExtension) RAMDiskControlDevice(&status3);
  if (!NT_SUCCESS(status3)) return status3;

  UNICODE_STRING sym_link;
  RtlInitUnicodeString(&sym_link, RAMDISK_CTL_SYMLINK_NAME_W);
  auto status2 = IoCreateUnprotectedSymbolicLink(&sym_link, &ctl_device_name);
  if (!NT_SUCCESS(status2)) {
    nt_log_error("Error while doing IoCreateUnprotectedSymbolicLink: %s (0x%x)",
		 nt_status_to_string(status2),
		 status2);
    return status2;
  }

  _delete_device_object.cancel();
  device_object->Flags &= ~DO_DEVICE_INITIALIZING;

  *out = device_object;

  return STATUS_SUCCESS;
}

void
delete_control_device(PDEVICE_OBJECT device_object) {
  auto dev = static_cast<RAMDiskControlDevice *>(device_object->DeviceExtension);
  dev->~RAMDiskControlDevice();

  UNICODE_STRING sym_link;
  RtlInitUnicodeString(&sym_link, RAMDISK_CTL_SYMLINK_NAME_W);
  IoDeleteSymbolicLink(&sym_link);

  IoDeleteDevice(device_object);
}

}

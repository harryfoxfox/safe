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

#include "tfs_dav_reparse_engage.hpp"

#include "ntoskrnl_cpp.hpp"
#include "nt_helpers.hpp"
#include "ramdisk_device.hpp"

#include <lockbox/deferred.hpp>

#include <ntifs.h>

#ifndef OBJ_KERNEL_HANDLE
#define OBJ_KERNEL_HANDLE       0x00000200
#endif

#ifndef OBJ_FORCE_ACCESS_CHECK
#define OBJ_FORCE_ACCESS_CHECK 0x00000400L
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
free_unicode_string(PUNICODE_STRING us) {
  ExFreePoolWithTag(us->Buffer, FREE_UNICODE_STRING_TAG);
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
set_delete_on_close(HANDLE file_handle) {
  FILE_DISPOSITION_INFORMATION file_dispo_info;
  memset(&file_dispo_info, 0, sizeof(file_dispo_info));
#ifdef DeleteFile
  file_dispo_info.DoDeleteFile = TRUE;
#else
  file_dispo_info.DeleteFile = TRUE;
#endif

  IO_STATUS_BLOCK io_status_block_2;
  auto status3 = ZwSetInformationFile(file_handle,
				      &io_status_block_2,
				      (PVOID) &file_dispo_info,
				      sizeof(file_dispo_info),
				      FileDispositionInformation);
  if (!NT_SUCCESS(status3)) {
    nt_log_error("Error while calling ZwSetInformationFile: %s (0x%x)",
		 nt_status_to_string(status3), status3);
    return status3;
  }

  return STATUS_SUCCESS;
}

static
NTSTATUS
does_tfs_dav_link_already_exists(PUNICODE_STRING path_to_tfs_dav,
				 PUNICODE_STRING intended_target,
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
			    FILE_SHARE_READ | FILE_SHARE_WRITE,
			    FILE_DIRECTORY_FILE
			    | FILE_OPEN_FOR_BACKUP_INTENT
			    | FILE_SYNCHRONOUS_IO_ALERT
			    | FILE_OPEN_REPARSE_POINT);
  if (!NT_SUCCESS(status2)) {
    if (status2 == STATUS_OBJECT_PATH_NOT_FOUND ||
	status2 == STATUS_OBJECT_NAME_NOT_FOUND) {
      return STATUS_SUCCESS;
    }
    else {
      nt_log_error("Error while calling ZwOpenFile(\"%wZ\"): %s (0x%x)",
		   path_to_tfs_dav,
		   nt_status_to_string(status2), status2);
      return status2;
    }
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

  if (RtlEqualUnicodeString(intended_target, &reparse_point_target, TRUE)) {
    // set delete on close
    auto status4 = set_delete_on_close(existing_tfs_dav_handle);
    if (!NT_SUCCESS(status4)) return status4;

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

  auto _free_new_tfs_dav_path =
    lockbox::create_deferred(free_unicode_string, &new_tfs_dav_path);

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
  auto status2 = ZwCreateFile(&existing_tfs_dav_handle,
			      DELETE | SYNCHRONIZE,
			      &attributes,
			      &io_status_block,
			      nullptr,
			      FILE_ATTRIBUTE_NORMAL,
			      FILE_SHARE_READ
			      | FILE_SHARE_WRITE
			      | FILE_SHARE_DELETE,
			      FILE_OPEN,
			      FILE_DIRECTORY_FILE
			      | FILE_SYNCHRONOUS_IO_ALERT
			      | FILE_OPEN_FOR_BACKUP_INTENT
			      | FILE_OPEN_REPARSE_POINT,
			      NULL, 0);
  if (!NT_SUCCESS(status2)) {
    if (status2 == STATUS_OBJECT_PATH_NOT_FOUND ||
	status2 == STATUS_OBJECT_NAME_NOT_FOUND) {
      // file doesn't exist, no need to do anything
      nt_log_debug("File \"%wZ\" didn't exist so not renaming", source_path);
      return STATUS_SUCCESS;
    }
    else {
      nt_log_error("Error while calling ZwCreateFile(\"%wZ\"): %s (0x%x)",
		   source_path, nt_status_to_string(status2), status2);
      return status2;
    }
  }

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
ensure_parent_dirs_exist(PUNICODE_STRING path_to_tfs_dav) {
  // path is of the form ...\TfsStore\Tfs_DAV
  // Create TfsStore

  UNICODE_STRING path_to_tfs_dav_parent = *path_to_tfs_dav;

  // find first '\'
  auto offset_to_slash = path_to_tfs_dav_parent.Length / sizeof(WCHAR) - 1;
  while (path_to_tfs_dav_parent.Buffer[offset_to_slash--] != L'\\') {}

  path_to_tfs_dav_parent.Length = (offset_to_slash + 1) * sizeof(WCHAR);

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes,
			     &path_to_tfs_dav_parent,
			     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			     nullptr,
			     nullptr);

  IO_STATUS_BLOCK io_status_block;
  HANDLE tfs_dav_handle;
  auto status = ZwCreateFile(&tfs_dav_handle,
			     GENERIC_READ | SYNCHRONIZE,
			     &attributes,
			     &io_status_block,
			     nullptr,
			     FILE_ATTRIBUTE_NORMAL,
			     FILE_SHARE_READ |
			     FILE_SHARE_WRITE |
			     FILE_SHARE_DELETE,
			     FILE_OPEN_IF,
			     FILE_DIRECTORY_FILE
			     | FILE_OPEN_FOR_BACKUP_INTENT
			     | FILE_SYNCHRONOUS_IO_NONALERT,
			     nullptr, 0);
  if (!NT_SUCCESS(status)) {
    nt_log_error("Error while calling ZwCreateFile(\"%wZ\"): %s (0x%x)",
		 &path_to_tfs_dav_parent,
		 nt_status_to_string(status), status);
    return status;
  }

  ZwClose(tfs_dav_handle);

  return STATUS_SUCCESS;
}

static
NTSTATUS
create_new_tfs_dav_link(PUNICODE_STRING path_to_tfs_dav,
			PUNICODE_STRING reparse_point_target,
			PHANDLE out_handle) {
  auto status0 = ensure_parent_dirs_exist(path_to_tfs_dav);
  if (!NT_SUCCESS(status0)) return status0;

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
			     FILE_SHARE_READ | FILE_SHARE_WRITE,
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

  const size_t total_size =
    8 + 8 + reparse_point_target->Length + 2 * sizeof(WCHAR);
  uint8_t reparse_data_buffer[total_size];
  memset(reparse_data_buffer, 0, sizeof(reparse_data_buffer));
  auto reparse_data = (PREPARSE_DATA_BUFFER) reparse_data_buffer;

  reparse_data->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  // NB: not exactly sure why there should be 2 extra characters
  //     at the end, my best guess is that we pretend the reparse
  //     point is null-terminated and have a place for PrintNameOffset
  //     to start (even though its length = 0)
  reparse_data->ReparseDataLength = (8 + reparse_point_target->Length
				     + 2 * sizeof(WCHAR));
  reparse_data->MountPointReparseBuffer.SubstituteNameLength =
    reparse_point_target->Length;
  reparse_data->MountPointReparseBuffer.PrintNameOffset =
    reparse_point_target->Length + sizeof(WCHAR);

  memcpy(reparse_data->MountPointReparseBuffer.PathBuffer,
	 reparse_point_target->Buffer,
	 reparse_point_target->Length);
  
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

NTSTATUS
engage_reparse_point(PHANDLE reparse_handle) {
  PAGED_CODE();

  nt_log_info("Engaging reparse point...");

  // get path to Tfs_DAV
  UNICODE_STRING path_to_tfs_dav;
  auto status1 = get_path_to_tfs_dav(&path_to_tfs_dav);
  if (!NT_SUCCESS(status1)) return status1;

  auto _free_tfs_path =
    lockbox::create_deferred(free_unicode_string, &path_to_tfs_dav);

  // get reparse point target
  UNICODE_STRING device_name;
  RtlInitUnicodeString(&device_name, RAMDISK_DEVICE_NAME);
  
  UNICODE_STRING trailing_slash;
  RtlInitUnicodeString(&trailing_slash, L"\\");

  uint8_t reparse_point_target_buf[device_name.Length
				   + trailing_slash.Length];
  UNICODE_STRING reparse_point_target;
  {
    reparse_point_target.Buffer = (PWSTR) reparse_point_target_buf;
    reparse_point_target.Length = 0;
    reparse_point_target.MaximumLength = sizeof(reparse_point_target_buf);

    RtlCopyUnicodeString(&reparse_point_target, &device_name);
    RtlAppendUnicodeStringToString(&reparse_point_target, &trailing_slash);
  }

  nt_log_debug("Reparse point target: \"%wZ\"",
	       &reparse_point_target);

  // check if link already exists
  auto status2 = does_tfs_dav_link_already_exists(&path_to_tfs_dav,
						  &reparse_point_target,
						  reparse_handle);
  if (!NT_SUCCESS(status2)) return status2;

  if (!*reparse_handle) {
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
    auto status4 = create_new_tfs_dav_link(&path_to_tfs_dav,
					   &reparse_point_target,
					   reparse_handle);
    if (!NT_SUCCESS(status4)) return status4;
  }
  else nt_log_debug("RAM disk reparse point already existed...");
    
  assert(*reparse_handle);

  nt_log_info("Engage complete");

  return STATUS_SUCCESS;
}

NTSTATUS
disengage_reparse_point(HANDLE reparse_handle) {
  PAGED_CODE();

  // close handle to dav link
  auto status = ZwClose(reparse_handle);
  if (!NT_SUCCESS(status)) {
    // NB: if we fail to close the handle, we won't be able to
    //     delete/move the directory and restore the old directory
    nt_log_error("Failure to close reparse handle, leaking...");
    return status;
  }

  // NB: at this point,
  //     the reparse point has been closed and deleted since it was opened
  //     with FILE_DELETE_ON_CLOSE

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
delete_tfs_dav_children(ULONGLONG expiration_age_100ns) {
  (void) expiration_age_100ns;
  nt_log_debug("DELET TFS DAV CHILDREN!\n");
  return STATUS_SUCCESS;
}

}

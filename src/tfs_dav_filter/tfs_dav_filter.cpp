/*
  Lockbox: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <tfs_dav_filter/ntoskrnl_cpp.hpp>

#include <lockbox/deferred.hpp>

#include <fltkernel.h>

// define a macro for now,
// other options include using a variadic template
// or figuration out stdarg in windows kernel space
// these are better options for type-safety reasons
#define log(...) (STATUS_SUCCESS == DbgPrint((char *) __VA_ARGS__))
#define log_debug log
#define log_info log
#define log_error log

namespace tfs_dav_filter {

PFLT_FILTER g_handle;

static
NTSTATUS
NTAPI
unload(FLT_FILTER_UNLOAD_FLAGS flagS) {
  FltUnregisterFilter(tfs_dav_filter::g_handle);
  log_info("goodbyte world!");
  return STATUS_SUCCESS;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
create(PFLT_CALLBACK_DATA args,
       PCFLT_RELATED_OBJECTS related_objcts,
       PVOID *completion_ctx) {
  PFLT_FILE_NAME_INFORMATION name_info = NULL;

  auto status = FltGetFileNameInformation(args,
					  FLT_FILE_NAME_NORMALIZED,
					  &name_info);
  if (!NT_SUCCESS(status)) {
    args->IoStatus.Status = status;
    return FLT_PREOP_COMPLETE;
  }
  auto _free_name_info =
    lockbox::create_deferred(FltReleaseFileNameInformation, name_info);

  ANSI_STRING ansi_string;
  RtlInitAnsiString(&ansi_string, NULL);
  auto status2 = RtlUnicodeStringToAnsiString(&ansi_string,
					      &name_info->Name,
					      TRUE);
  if (!NT_SUCCESS(status2)) {
    args->IoStatus.Status = status2;
    return FLT_PREOP_COMPLETE;
  }
  auto _free_ansi_string =
    lockbox::create_deferred(RtlFreeAnsiString, &ansi_string);
  
  log_debug("creating: %s", ansi_string.Buffer);

  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
create_named_pipe(PFLT_CALLBACK_DATA args,
		  PCFLT_RELATED_OBJECTS related_objects,
		  PVOID *completion_ctx) {
  log_debug("create_named_pipe");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
close(PFLT_CALLBACK_DATA args,
      PCFLT_RELATED_OBJECTS related_objects,
      PVOID *completion_ctx) {
  log_debug("close");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
read(PFLT_CALLBACK_DATA args,
      PCFLT_RELATED_OBJECTS related_objects,
      PVOID *completion_ctx) {
  log_debug("read");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
write(PFLT_CALLBACK_DATA args,
      PCFLT_RELATED_OBJECTS related_objects,
      PVOID *completion_ctx) {
  log_debug("write");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
query_information(PFLT_CALLBACK_DATA args,
		  PCFLT_RELATED_OBJECTS related_objects,
		  PVOID *completion_ctx) {
  log_debug("query_information");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
set_information(PFLT_CALLBACK_DATA args,
		PCFLT_RELATED_OBJECTS related_objects,
		PVOID *completion_ctx) {
  log_debug("set_information");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
query_ea(PFLT_CALLBACK_DATA args,
	 PCFLT_RELATED_OBJECTS related_objects,
	 PVOID *completion_ctx) {
  log_debug("query_ea");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
set_ea(PFLT_CALLBACK_DATA args,
       PCFLT_RELATED_OBJECTS related_objects,
       PVOID *completion_ctx) {
  log_debug("set_ea");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
flush_buffers(PFLT_CALLBACK_DATA args,
	      PCFLT_RELATED_OBJECTS related_objects,
	      PVOID *completion_ctx) {
  log_debug("flush_buffers");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
query_volume_information(PFLT_CALLBACK_DATA args,
			 PCFLT_RELATED_OBJECTS related_objects,
			 PVOID *completion_ctx) {
  log_debug("query_volume_information");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
set_volume_information(PFLT_CALLBACK_DATA args,
		       PCFLT_RELATED_OBJECTS related_objects,
		       PVOID *completion_ctx) {
  log_debug("set_volume_information");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
directory_control(PFLT_CALLBACK_DATA args,
		  PCFLT_RELATED_OBJECTS related_objects,
		  PVOID *completion_ctx) {
  log_debug("directory_control");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
file_system_control(PFLT_CALLBACK_DATA args,
		    PCFLT_RELATED_OBJECTS related_objects,
		    PVOID *completion_ctx) {
  log_debug("file_system_control");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
device_control(PFLT_CALLBACK_DATA args,
	       PCFLT_RELATED_OBJECTS related_objects,
	       PVOID *completion_ctx) {
  log_debug("device_control");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
internal_device_control(PFLT_CALLBACK_DATA args,
			PCFLT_RELATED_OBJECTS related_objects,
			PVOID *completion_ctx) {
  log_debug("internal_device_control");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
shutdown(PFLT_CALLBACK_DATA args,
	 PCFLT_RELATED_OBJECTS related_objects,
	 PVOID *completion_ctx) {
  log_debug("shutdown");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
lock_control(PFLT_CALLBACK_DATA args,
	     PCFLT_RELATED_OBJECTS related_objects,
	     PVOID *completion_ctx) {
  log_debug("lock_control");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
cleanup(PFLT_CALLBACK_DATA args,
	PCFLT_RELATED_OBJECTS related_objects,
	PVOID *completion_ctx) {
  log_debug("cleanup");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
create_mailslot(PFLT_CALLBACK_DATA args,
		PCFLT_RELATED_OBJECTS related_objects,
		PVOID *completion_ctx) {
  log_debug("create_mailslot");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
query_security(PFLT_CALLBACK_DATA args,
	       PCFLT_RELATED_OBJECTS related_objects,
	       PVOID *completion_ctx) {
  log_debug("query_security");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
set_security(PFLT_CALLBACK_DATA args,
	     PCFLT_RELATED_OBJECTS related_objects,
	     PVOID *completion_ctx) {
  log_debug("set_security");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
query_quota(PFLT_CALLBACK_DATA args,
	    PCFLT_RELATED_OBJECTS related_objects,
	    PVOID *completion_ctx) {
  log_debug("query_quota");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
set_quota(PFLT_CALLBACK_DATA args,
	  PCFLT_RELATED_OBJECTS related_objects,
	  PVOID *completion_ctx) {
  log_debug("set_quota");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
pnp(PFLT_CALLBACK_DATA args,
    PCFLT_RELATED_OBJECTS related_objects,
    PVOID *completion_ctx) {
  log_debug("pnp");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
acquire_for_section_synchronization(PFLT_CALLBACK_DATA args,
				    PCFLT_RELATED_OBJECTS related_objects,
				    PVOID *completion_ctx) {
  log_debug("acquire_for_section_synchronization");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
release_for_section_synchronization(PFLT_CALLBACK_DATA args,
				    PCFLT_RELATED_OBJECTS related_objects,
				    PVOID *completion_ctx) {
  log_debug("release_for_section_synchronization");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
acquire_for_mod_write(PFLT_CALLBACK_DATA args,
		      PCFLT_RELATED_OBJECTS related_objects,
		      PVOID *completion_ctx) {
  log_debug("acquire_for_mod_write");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
release_for_mod_write(PFLT_CALLBACK_DATA args,
		      PCFLT_RELATED_OBJECTS related_objects,
		      PVOID *completion_ctx) {
  log_debug("release_for_mod_write");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
acquire_for_cc_flush(PFLT_CALLBACK_DATA args,
		     PCFLT_RELATED_OBJECTS related_objects,
		     PVOID *completion_ctx) {
  log_debug("acquire_for_cc_flush");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
release_for_cc_flush(PFLT_CALLBACK_DATA args,
		     PCFLT_RELATED_OBJECTS related_objects,
		     PVOID *completion_ctx) {
  log_debug("release_for_cc_flush");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
fast_io_check_if_possible(PFLT_CALLBACK_DATA args,
			  PCFLT_RELATED_OBJECTS related_objects,
			  PVOID *completion_ctx) {
  log_debug("fast_io_check_if_possible");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
network_query_open(PFLT_CALLBACK_DATA args,
		   PCFLT_RELATED_OBJECTS related_objects,
		   PVOID *completion_ctx) {
  log_debug("network_query_open");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
mdl_read(PFLT_CALLBACK_DATA args,
	 PCFLT_RELATED_OBJECTS related_objects,
	 PVOID *completion_ctx) {
  log_debug("mdl_read");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
  
static
FLT_PREOP_CALLBACK_STATUS
NTAPI
mdl_read_complete(PFLT_CALLBACK_DATA args,
		  PCFLT_RELATED_OBJECTS related_objects,
		  PVOID *completion_ctx) {
  log_debug("mdl_read_complete");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
prepare_mdl_write(PFLT_CALLBACK_DATA args,
		  PCFLT_RELATED_OBJECTS related_objects,
		  PVOID *completion_ctx) {
  log_debug("prepare_mdl_write");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
FLT_PREOP_CALLBACK_STATUS
NTAPI
mdl_write_complete(PFLT_CALLBACK_DATA args,
		   PCFLT_RELATED_OBJECTS related_objects,
		   PVOID *completion_ctx) {
  log_debug("mdl_write_complete");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static  
FLT_PREOP_CALLBACK_STATUS
NTAPI
volume_mount(PFLT_CALLBACK_DATA args,
	     PCFLT_RELATED_OBJECTS related_objects,
	     PVOID *completion_ctx) {
  log_debug("volume_mount");
  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

const
FLT_OPERATION_REGISTRATION
operations[] = {
  {IRP_MJ_CREATE, 0, create, nullptr},
  {IRP_MJ_CREATE_NAMED_PIPE, 0, create_named_pipe, nullptr},
  {IRP_MJ_CLOSE, 0, close, nullptr},
  {IRP_MJ_READ, 0, read, nullptr},
  {IRP_MJ_WRITE, 0, write, nullptr},
  {IRP_MJ_QUERY_INFORMATION, 0, query_information, nullptr},
  {IRP_MJ_SET_INFORMATION, 0, set_information, nullptr},
  {IRP_MJ_QUERY_EA, 0, query_ea, nullptr},
  {IRP_MJ_SET_EA, 0, set_ea, nullptr},
  {IRP_MJ_FLUSH_BUFFERS, 0, flush_buffers, nullptr},
  {IRP_MJ_QUERY_VOLUME_INFORMATION, 0, query_volume_information, nullptr},
  {IRP_MJ_SET_VOLUME_INFORMATION, 0, set_volume_information, nullptr},
  {IRP_MJ_DIRECTORY_CONTROL, 0, directory_control, nullptr},
  {IRP_MJ_FILE_SYSTEM_CONTROL, 0, file_system_control, nullptr},
  {IRP_MJ_DEVICE_CONTROL, 0, device_control, nullptr},
  {IRP_MJ_INTERNAL_DEVICE_CONTROL, 0, internal_device_control, nullptr},
  {IRP_MJ_SHUTDOWN, 0, shutdown, nullptr},
  {IRP_MJ_LOCK_CONTROL, 0, lock_control, nullptr},
  {IRP_MJ_CLEANUP, 0, cleanup, nullptr},
  {IRP_MJ_CREATE_MAILSLOT, 0, create_mailslot, nullptr},
  {IRP_MJ_QUERY_SECURITY, 0, query_security, nullptr},
  {IRP_MJ_SET_SECURITY, 0, set_security, nullptr},
  {IRP_MJ_QUERY_QUOTA, 0, query_quota, nullptr},
  {IRP_MJ_SET_QUOTA, 0, set_quota, nullptr},
  {IRP_MJ_PNP, 0, pnp, nullptr},
  {IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION, 0,
   acquire_for_section_synchronization, nullptr},
  {IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION, 0,
   release_for_section_synchronization, nullptr},
  {IRP_MJ_ACQUIRE_FOR_MOD_WRITE, 0, acquire_for_mod_write, nullptr},
  {IRP_MJ_RELEASE_FOR_MOD_WRITE, 0, release_for_mod_write, nullptr},
  {IRP_MJ_ACQUIRE_FOR_CC_FLUSH, 0, acquire_for_cc_flush, nullptr},
  {IRP_MJ_RELEASE_FOR_CC_FLUSH, 0, release_for_cc_flush, nullptr},
  {IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE, 0,
   fast_io_check_if_possible, nullptr},
  {IRP_MJ_NETWORK_QUERY_OPEN, 0, network_query_open, nullptr},
  {IRP_MJ_MDL_READ, 0, mdl_read, nullptr},
  {IRP_MJ_MDL_READ_COMPLETE, 0, mdl_read_complete, nullptr},
  {IRP_MJ_PREPARE_MDL_WRITE, 0, prepare_mdl_write, nullptr},
  {IRP_MJ_MDL_WRITE_COMPLETE, 0, mdl_write_complete, nullptr},
  {IRP_MJ_VOLUME_MOUNT, 0, volume_mount, nullptr},
  {IRP_MJ_OPERATION_END},
};

const
FLT_REGISTRATION
registration = {
  sizeof(registration),
  FLT_REGISTRATION_VERSION,
  0,
  nullptr,
  operations,
  unload,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
};
  
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath) {
  log_info("hello world!");

  auto res = FltRegisterFilter(DriverObject,
			       &tfs_dav_filter::registration,
			       &tfs_dav_filter::g_handle);
  if (!NT_SUCCESS(res)) {
    log_error("couldn't register filter...");
    return res;
  }

  auto res_start = FltStartFiltering(tfs_dav_filter::g_handle);
  if (!NT_SUCCESS(res_start)) {
    FltUnregisterFilter(tfs_dav_filter::g_handle);
  }

  return res_start;
}



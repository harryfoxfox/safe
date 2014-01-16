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

#include <ntoskrnl_cpp.hpp>

#include <fltkernel.h>

namespace tfs_dav_filter {

bool
log(const char *fmt) {
  auto ret = DbgPrint((char *) fmt);
  return ret == STATUS_SUCCESS;
}

const auto & log_info = log;
const auto & log_error = log;

PFLT_FILTER g_handle;

NTSTATUS
NTAPI
unload(FLT_FILTER_UNLOAD_FLAGS flagS) {
  FltUnregisterFilter(tfs_dav_filter::g_handle);
  log_info("goodbyte world!");
  return STATUS_SUCCESS;
}

const
FLT_REGISTRATION
registration = {
  sizeof(registration),
  FLT_REGISTRATION_VERSION,
  0,
  nullptr,
  nullptr,
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
  tfs_dav_filter::log_info("hello world!");

  auto res = FltRegisterFilter(DriverObject,
			       &tfs_dav_filter::registration,
			       &tfs_dav_filter::g_handle);
  if (!NT_SUCCESS(res)) {
    tfs_dav_filter::log_error("couldn't register filter...");
    return res;
  }

  auto res_start = FltStartFiltering(tfs_dav_filter::g_handle);
  if (!NT_SUCCESS(res_start)) {
    FltUnregisterFilter(tfs_dav_filter::g_handle);
  }

  return res_start;
}



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

#include <lockbox/ramdisk_win.hpp>

#include <lockbox/deferred.hpp>
#include <lockbox/resources_win.h>
#include <lockbox/util.hpp>
#include <lockbox/windows_error.hpp>
#include <lockbox/windows_string.hpp>

#include <safe_ramdisk/ramdisk_ioctl.h>

#include <string>

#include <windows.h>
#include <setupapi.h>
#include <psapi.h>

#ifndef MAX_CLASS_NAME_LEN
#define MAX_CLASS_NAME_LEN 1024
#endif

namespace lockbox { namespace win {

bool
need_to_install_kernel_driver() {
  // check if we can access the ramdisk
  HANDLE hFile = CreateFileW(L"\\\\.\\" RAMDISK_CTL_DOSNAME_W,
			     0, 0,
			     NULL,
			     OPEN_EXISTING,
			     0,
			     NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    throw w32util::windows_error();
  }

  CloseHandle(hFile);
  
  return false;
}

class BinaryResource {
  void *_data;
  size_t _size;
public:
  BinaryResource(void *data, size_t size)
    : _data(data)
    , _size(size) {}

  size_t
  get_size() { return _size; }

  void *
  get_data() { return _data; }
};

static
BinaryResource
load_resource_data(LPCWSTR name, LPCWSTR type) {
  auto rsrc = FindResource(nullptr, name, type);
  if (!rsrc) throw w32util::windows_error();

  auto loaded_rsrc = LoadResource(nullptr, rsrc);
  if (!loaded_rsrc) throw w32util::windows_error();

  auto res_data_ptr = LockResource(loaded_rsrc);
  if (!res_data_ptr) throw w32util::windows_error();

  auto size_of_data = SizeofResource(nullptr, rsrc);
  if (!size_of_data) throw w32util::windows_error();

  return BinaryResource(res_data_ptr, size_of_data);
}

static
void
write_file(std::string path, void *data, size_t size) {
  auto hfile = CreateFileW(w32util::widen(path).c_str(),
                           GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (hfile == INVALID_HANDLE_VALUE) throw w32util::windows_error();

  auto _close_file = lockbox::create_deferred(CloseHandle, hfile);

  DWORD bytes_written;
  auto success = WriteFile(hfile, data, size, &bytes_written, NULL);
  if (!success || bytes_written != size) {
    throw w32util::windows_error();
  }
}

static
void
store_resource_to_file(LPCWSTR name, LPCWSTR type,
                       std::string path) {
  auto data = load_resource_data(name, type);
  write_file(path, data.get_data(), data.get_size());  
}

static
size_t
query_commit_limit() {
  PERFORMANCE_INFORMATION perf_info;
  perf_info.cb = sizeof(perf_info);
  auto success = GetPerformanceInfo(&perf_info, sizeof(perf_info));
  if (!success) throw w32util::windows_error();
  return (size_t) (perf_info.CommitLimit * perf_info.PageSize);

}

static
void
create_ramdisk_software_keys(HDEVINFO device_info_set,
                             PSP_DEVINFO_DATA device_info_data) {
  // we allow the ramdisk to use 1/5 of the commit limit
  // (the commit limit shouldn't be larger than the largest DWORD
  //  value)
  auto ramdisk_size =
    (DWORD) (std::min(query_commit_limit(),
                      (size_t) MAXDWORD) / 5);

  auto hkey = SetupDiCreateDevRegKey(device_info_set,
                                     device_info_data,
                                     DICS_FLAG_GLOBAL,
                                     0,
                                     DIREG_DRV,
                                     nullptr,
                                     nullptr);
  if (hkey == INVALID_HANDLE_VALUE) throw w32util::windows_error();

  auto _close_key = lockbox::create_deferred(RegCloseKey, hkey);

  auto ret = RegSetValueEx(hkey,
                           SAFE_RAMDISK_SIZE_VALUE_NAME_W,
                           0,
                           REG_DWORD,
                           (BYTE *) &ramdisk_size,
                           sizeof(ramdisk_size));
  if (ret != ERROR_SUCCESS) throw w32util::windows_error();
}

static
void
create_ramdisk_device(std::string full_inf_file_path,
                      std::string hardware_id) {
  CHAR class_name[MAX_CLASS_NAME_LEN];
  GUID class_guid;
  auto success =
    SetupDiGetINFClassA(full_inf_file_path.c_str(),
                        &class_guid,
                        class_name, sizeof(class_name),
                        nullptr);
  if (!success) throw w32util::windows_error();

  /* don't install device if it already exists */
  auto create_device = true;
  {
    HDEVINFO device_info_set =
      SetupDiGetClassDevsEx(&class_guid, nullptr, nullptr, 0,
			    nullptr, nullptr, nullptr);
    if (device_info_set == INVALID_HANDLE_VALUE) {
      throw w32util::windows_error();
    }

    SP_DEVINFO_DATA device_info_data;
    zero_object(device_info_data);
    device_info_data.cbSize = sizeof(device_info_data);
    for (DWORD idx = 0;
	 create_device &&
	 SetupDiEnumDeviceInfo(device_info_set, idx, &device_info_data);
	 ++idx) {
      /* first get hardware id reg key for this device info */
      CHAR buffer[1024];
      BOOL success_prop =
	SetupDiGetDeviceRegistryPropertyA(device_info_set,
					  &device_info_data,
					  SPDRP_HARDWAREID,
					  nullptr,
					  (PBYTE) buffer,
					  sizeof(buffer),
					  nullptr);
      if (!success_prop) throw w32util::windows_error();

      PCHAR bp = buffer;
      while(*bp) {
	if (!strcmp(bp, hardware_id.c_str())) {
	  create_device = false;
	  break;
	}
	bp += strlen(bp) + 1;
      }
    }
  }

  // device already exists, no need to create it
  if (!create_device) return;

  auto device_info_set =
    SetupDiCreateDeviceInfoList(&class_guid, NULL);
  if (device_info_set == INVALID_HANDLE_VALUE) throw w32util::windows_error();

  SP_DEVINFO_DATA device_info_data;
  zero_object(device_info_data);
  device_info_data.cbSize = sizeof(device_info_data);
  auto success_create_device_info =
    SetupDiCreateDeviceInfoA(device_info_set, class_name,
                             &class_guid, nullptr, 0,
                             DICD_GENERATE_ID, &device_info_data);
  if (!success_create_device_info) throw w32util::windows_error();

  auto success_set_hardware_id =
    SetupDiSetDeviceRegistryPropertyA(device_info_set,
                                      &device_info_data,
                                      SPDRP_HARDWAREID,
                                      (BYTE *) hardware_id.c_str(),
                                      (DWORD) hardware_id.size() + 1);
  if (!success_set_hardware_id) throw w32util::windows_error();

  auto success_class_installer =
    SetupDiCallClassInstaller(DIF_REGISTERDEVICE, device_info_set,
                              &device_info_data);
  if (!success_class_installer) throw w32util::windows_error();

  create_ramdisk_software_keys(device_info_set, &device_info_data);
}

extern "C"
BOOL UpdateDriverForPlugAndPlayDevicesW(HWND hwndParent,
                                        LPCWSTR HardwareId,
                                        LPCWSTR FullInfPath,
                                        DWORD InstallFlags,
                                        PBOOL bRebootRequired);

bool
install_kernel_driver() {
  // if this is a 64-bit system, we can't install any driver
  // TODO:

  // write out .sys and .inf resources to a temp file

  // create temp directory
  WCHAR temp_path[MAX_PATH + 1];
  auto ret = GetTempPathW(numelementsf(temp_path), temp_path);
  if (!ret) throw w32util::windows_error();

  auto temp_dir = std::wstring(temp_path, ret) + L"saferamdisk";
  lbx_log_debug("Creating debug directory at %ls", temp_dir.c_str());
  auto success = CreateDirectoryW(temp_dir.c_str(), nullptr);
  if (!success && GetLastError() != ERROR_ALREADY_EXISTS) {
    throw w32util::windows_error();
  }

  // store resource data to disk
  auto inf_file_path = w32util::narrow(temp_dir) + "\\safe_ramdisk.inf";
  store_resource_to_file(ID_LBX_RD_INF, LBX_BIN_RSRC,
                         inf_file_path);
  store_resource_to_file(ID_LBX_RD_SYS32, LBX_BIN_RSRC,
                         w32util::narrow(temp_dir) +
                         "\\safe_ramdisk.sys");

  // create root-device
  // NB: hardware_id must match what's in the inf file
  auto hardware_id = std::string("root\\saferamdisk");
  create_ramdisk_device(inf_file_path, hardware_id);

  // update driver on root-device
  // XXX: if we're on 64-bit windows this won't work
  // http://msdn.microsoft.com/en-us/library/windows/hardware/ff541255%28v=vs.85%29.aspx
  DWORD INSTALLFLAG_FORCE = 0x1;
  BOOL restart_required;
  auto success2 =
    UpdateDriverForPlugAndPlayDevicesW(NULL,
                                       w32util::widen(hardware_id).c_str(),
                                       w32util::widen(inf_file_path).c_str(),
                                       INSTALLFLAG_FORCE,
                                       &restart_required);
  if (!success2) throw w32util::windows_error();

  return restart_required;
}

RAMDiskHandle
engage_ramdisk() {
  // check if we can access the ramdisk
  auto hFile = CreateFileW(L"\\\\.\\" RAMDISK_CTL_DOSNAME_W,
                           0, 0,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
  if (hFile == INVALID_HANDLE_VALUE) throw w32util::windows_error();

  auto toret = RAMDiskHandle(hFile);

  // TODO: could refactor RAMDiskHandle to have an engage method
  DWORD dw;
  auto success = DeviceIoControl(hFile,
				 IOCTL_SAFE_RAMDISK_ENGAGE,
				 NULL,
				 0,
				 NULL,
				 0,
				 &dw,
				 NULL);
  if (!success) throw w32util::windows_error();

  return toret;
}

}}

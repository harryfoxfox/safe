/*
  install_non_pnp_device:
  Create a root-enumerated PnP device and install drivers for it.
  
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

#include <stdbool.h>
#include <stdio.h>

#include <windows.h>
#include <setupapi.h>

#ifndef MAX_CLASS_NAME_LEN
#define MAX_CLASS_NAME_LEN 1024
#endif

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Incorrect arguments!\n");
    return EXIT_FAILURE;
  }

  PCHAR inf_file_path = argv[1];
  PCHAR hardware_id = argv[2];

  /* install root-enumerated ramdisk device if it doesn't exist */
  CHAR full_inf_file_path[MAX_PATH];
  DWORD ret_get_full_path =
    GetFullPathNameA(inf_file_path, sizeof(full_inf_file_path),
		     full_inf_file_path, NULL);
  if (!ret_get_full_path) {
    fprintf(stderr, "Failed to get full path for %s\n", inf_file_path);
    return EXIT_FAILURE;
  }

  CHAR class_name[MAX_CLASS_NAME_LEN];
  GUID class_guid;
  BOOL success =
    SetupDiGetINFClass(full_inf_file_path, &class_guid,
		       class_name, sizeof(class_name),
		       NULL);
  if (!success) {
    fprintf(stderr, "Failed to get class name/guid for: %s\n",
	    full_inf_file_path);
    return EXIT_FAILURE;
  }

  /* don't install device if it already exists */
  BOOL create_device = true;
  {
    HDEVINFO device_info_set =
      SetupDiGetClassDevsEx(&class_guid, NULL, NULL, 0,
			    NULL, NULL, NULL);
    if (device_info_set == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "Failed to create device info set\n");
      return EXIT_FAILURE;
    }

    SP_DEVINFO_DATA device_info_data = {
      .cbSize = sizeof(device_info_data),
    };
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
					  NULL,
					  (PBYTE) buffer,
					  sizeof(buffer),
					  NULL);
      if (!success_prop) {
	fprintf(stderr, "Failed to call SetupDiGetDeviceRegistryPropertyA");
	return EXIT_FAILURE;
      }

      PCHAR bp = buffer;
      while(*bp) {
	if (!strcmp(bp, hardware_id)) {
	  create_device = false;
	  break;
	}
	bp += strlen(bp) + 1;
      }
    }
  }

  if (create_device) {
    printf("Creating device\n");
    printf("hardware id: %s\n", hardware_id);
    printf("class name %s\n", class_name);

    HDEVINFO device_info_set =
      SetupDiCreateDeviceInfoList(&class_guid, NULL);
    if (device_info_set == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "Failed to create device info set\n");
      return EXIT_FAILURE;
    }

    SP_DEVINFO_DATA device_info_data = {
      .cbSize = sizeof(device_info_data),
    };
    BOOL success_create_device_info =
      SetupDiCreateDeviceInfo(device_info_set, class_name,
			      &class_guid, NULL, 0,
			      DICD_GENERATE_ID, &device_info_data);
    if (!success_create_device_info) {
      fprintf(stderr, "Failed to create device info data\n");
      return EXIT_FAILURE;
    }

    BOOL success_set_hardware_id =
      SetupDiSetDeviceRegistryPropertyA(device_info_set,
					&device_info_data,
					SPDRP_HARDWAREID,
					(BYTE *) hardware_id,
					(DWORD) strlen(hardware_id) + 1);
    if (!success_set_hardware_id) {
      fprintf(stderr, "Failed to set hardware id registry property\n");
      return EXIT_FAILURE;
    }

    BOOL success_class_installer =
      SetupDiCallClassInstaller(DIF_REGISTERDEVICE, device_info_set,
				&device_info_data);
    if (!success_class_installer) {
      fprintf(stderr, "Failed to call class installer\n");
      return EXIT_FAILURE;
    }
  }
  else {
    printf("Device already exists!\n");
  }

  /* then update driver for it */
  /* first declare function */
  BOOL UpdateDriverForPlugAndPlayDevicesA(HWND hwndParent,
					  LPCSTR HardwareId,
					  LPCSTR FullInfPath,
					  DWORD InstallFlags,
					  PBOOL bRebootRequired);

  DWORD INSTALLFLAG_FORCE = 0x1;
  BOOL restart_required;
  BOOL success_4 =
    UpdateDriverForPlugAndPlayDevicesA(NULL,
				       hardware_id,
				       full_inf_file_path,
				       INSTALLFLAG_FORCE,
				       &restart_required);
  if (!success_4) {
    fprintf(stderr, "Failed to update driver: %d\n",
	    (int) GetLastError());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

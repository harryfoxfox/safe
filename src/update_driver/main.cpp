/*
  Safe: Encrypted File System
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

#include <safe/win/ramdisk.hpp>
#include <w32util/error.hpp>
#include <w32util/string.hpp>

#include <windows.h>
#include <shellapi.h>

extern "C"
int main() {
  int argc;
  auto wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wargv) return GetLastError();

  if (argc != 3) return ERROR_INVALID_PARAMETER;

  auto hardware_id = w32util::narrow(wargv[1]);
  auto inf_file_path = w32util::narrow(wargv[2]);

  int ret;
  try {
    auto restart_required =
      safe::win::create_device_and_install_driver_native(hardware_id,
							    inf_file_path);
    ret = restart_required
      ? ERROR_SUCCESS_REBOOT_REQUIRED
      : ERROR_SUCCESS;
  }
  catch (const w32util::windows_error & err) {
    ret = err.code().value();
  }
  catch (const std::exception & err) {
    ret = -1;
  }

  return ret;
}

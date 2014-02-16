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

#include <lockbox/windows_file.hpp>

#include <lockbox/deferred.hpp>
#include <lockbox/windows_error.hpp>
#include <lockbox/windows_string.hpp>

#include <memory>

#include <windows.h>

namespace w32util {

std::string
get_temp_path() {
  WCHAR temp_path[MAX_PATH + 1];
  auto ret = GetTempPathW(sizeof(temp_path) / sizeof(temp_path[0]),
                          temp_path);
  if (!ret) w32util::throw_windows_error();
  return w32util::narrow(temp_path, ret);
}

std::string
get_temp_file_name(std::string root, std::string prefix) {
  WCHAR buffer[MAX_PATH + 1];
  check_call(0, GetTempFileNameW,
             w32util::widen(root).c_str(),
             w32util::widen(prefix).c_str(),
             0, buffer);
  return w32util::narrow(buffer);
}

Buffer
read_file(HANDLE handle, DWORD num_bytes_to_read) {
  auto ptr = std::unique_ptr<uint8_t[]>(new uint8_t[num_bytes_to_read]);
  DWORD bytes_read;
  w32util::check_bool(ReadFile, handle, ptr.get(),
                      num_bytes_to_read, &bytes_read,
                      nullptr);
  return Buffer {std::move(ptr), bytes_read};
}

bool
file_exists(std::string file_path) {
  auto file_attributes =
    GetFileAttributesW(w32util::widen(file_path).c_str());
  if (file_attributes == INVALID_FILE_ATTRIBUTES) {
    auto last_error = GetLastError();
    if (last_error == ERROR_FILE_NOT_FOUND ||
        last_error == ERROR_PATH_NOT_FOUND) {
      return false;
    }
    throw w32util::windows_error(last_error);
  }

  return true;
}

bool
map_to_same_target(std::string a, std::string b) {
  auto a_handle =
    w32util::check_invalid_handle(CreateFileW,
                                  w32util::widen(a).c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  auto close_a =
    lockbox::create_deferred(CloseHandle, a_handle);

  auto b_handle =
    w32util::check_invalid_handle(CreateFileW,
                                  w32util::widen(b).c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  auto close_b =
    lockbox::create_deferred(CloseHandle, b_handle);

  BY_HANDLE_FILE_INFORMATION a_info, b_info;

  w32util::check_bool(GetFileInformationByHandle, a_handle,
                      &a_info);

  w32util::check_bool(GetFileInformationByHandle, b_handle,
                      &b_info);

  return ((a_info.dwVolumeSerialNumber == b_info.dwVolumeSerialNumber) &&
          (a_info.nFileSizeHigh == b_info.nFileSizeHigh) &&
          (a_info.nFileSizeLow == b_info.nFileSizeLow));
}

void
ensure_deleted(std::string path) {
  try {
    w32util::check_bool(DeleteFileW, w32util::widen(path).c_str());
  }
  catch (const std::system_error & err) {
    if (err.code() != std::errc::no_such_file_or_directory) throw;
  }
}

}

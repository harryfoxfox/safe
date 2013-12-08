/*
  Lockbox: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

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

#ifndef __lockbox_windows_error_hpp
#define __lockbox_windows_error_hpp

#include <stdexcept>

#include <lockbox/lean_windows.h>

namespace w32util {

inline
std::string
error_message(DWORD err_code) {
  enum {
    MAX_MSG=128,
  };
  wchar_t error_buf_wide[MAX_MSG];
  char error_buf[MAX_MSG];

  DWORD num_chars =
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, 0, err_code, 0,
                   error_buf_wide,
                   sizeof(error_buf_wide) / sizeof(error_buf_wide[0]),
                   NULL);
  if (!num_chars) {
    return "Couldn't get error message, FormatMessageW() failed";
  }

  // clear \r\n
  if (num_chars >= 2) {
    error_buf_wide[num_chars - 2] = L'\0';
    num_chars -= 2;
  }

  const DWORD flags = 0;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        error_buf_wide, num_chars,
                        error_buf, sizeof(error_buf),
                        NULL, NULL);
  if (!required_buffer_size) {
    return "Couldn't get error_message, WideCharToMultibyte() failed";
  }

  return std::string(error_buf, required_buffer_size);
}

inline
std::string
last_error_message() {
  return error_message(GetLastError());
}

// TODO: implement
inline
std::runtime_error
windows_error() {
  return std::runtime_error("windows error to go here");
}

}

#endif

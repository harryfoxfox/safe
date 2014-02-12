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

#include <lockbox/logging.h>
#include <lockbox/lean_windows.h>

#include <encfs/base/optional.h>

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
last_error_message(opt::optional<DWORD> err_code = opt::nullopt) {
  if (!err_code) err_code = GetLastError();
  return error_message(*err_code);
}

// TODO: implement class
inline
std::runtime_error
windows_error(opt::optional<DWORD> err_code = opt::nullopt) {
  return std::runtime_error(last_error_message(std::move(err_code)));
}

inline
void
throw_windows_error() {
  // NB: for some reason on g++ the last error is reset 
  //     when called on the same line as "throw"
  auto err_code = GetLastError();
  throw windows_error(err_code);
}

template <class F, class... Args,
          class ReturnType = typename std::result_of<F(Args...)>::type>
ReturnType
check_call(ReturnType bad, F && f, Args && ...args) {
  auto ret = std::forward<F>(f)(std::forward<Args>(args)...);
  if (ret == bad) w32util::throw_windows_error();
  return ret;
}

template <class F, class... Args>
HANDLE
check_invalid_handle(F && f, Args && ...args) {
  return check_call(INVALID_HANDLE_VALUE,
                    std::forward<F>(f),
                    std::forward<Args>(args)...);
}

template <class F, class... Args>
void
check_bool(F && f, Args && ...args) {
  check_call(FALSE,
             std::forward<F>(f),
             std::forward<Args>(args)...);
}


}

#endif

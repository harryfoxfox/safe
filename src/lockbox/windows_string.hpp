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

#ifndef __lockbox_windows_string_hpp
#define __lockbox_windows_string_hpp

#include <lockbox/windows_error.hpp>

#include <memory>
#include <stdexcept>
#include <string>

#include <cstddef>

#include <lockbox/lean_windows.h>

namespace w32util {

inline
std::wstring
widen(const std::string & s) {
  if (s.empty()) return std::wstring();

  /* TODO: are these flags good? */
  const DWORD flags = /*MB_COMPOSITE | */MB_ERR_INVALID_CHARS;

  const int required_buffer_size =
    MultiByteToWideChar(CP_UTF8, flags,
                        s.data(), s.size(), NULL, 0);
  if (!required_buffer_size) throw std::runtime_error("error");

  auto out = std::unique_ptr<wchar_t[]>(new wchar_t[required_buffer_size]);

  const int new_return =
    MultiByteToWideChar(CP_UTF8, flags,
                        s.data(), s.size(),
                        out.get(), required_buffer_size);
  if (!new_return) throw std::runtime_error("error");

  return std::wstring(out.get(), required_buffer_size);
}

inline
size_t
narrow_into_buf(const wchar_t *s, size_t num_chars,
                char *out, size_t buf_size_in_bytes) {
  if (!num_chars) return 0;
  DWORD flags = 0 /*| WC_ERR_INVALID_CHARS*/;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        s, num_chars,
                        out, buf_size_in_bytes,
                        NULL, NULL);
  if (!required_buffer_size) throw w32util::windows_error();
  return required_buffer_size;
}

inline
std::string
narrow(const wchar_t *s, size_t num_chars) {
  if (!num_chars) return std::string();

  /* WC_ERR_INVALID_CHARS is only on windows vista and later */
  DWORD flags = 0 /*| WC_ERR_INVALID_CHARS*/;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        s, num_chars,
                        NULL, 0,
                        NULL, NULL);
  if (!required_buffer_size) throw std::runtime_error("error");

  auto out = std::unique_ptr<char[]>(new char[required_buffer_size]);

  const int new_return =
    WideCharToMultiByte(CP_UTF8, flags,
                        s, num_chars,
                        out.get(), required_buffer_size,
                        NULL, NULL);
  if (!new_return) throw std::runtime_error("error");

  return std::string(out.get(), required_buffer_size);
}

inline
std::string
narrow(const std::wstring & s) {
  return narrow(s.data(), s.size());
}

inline
size_t
num_characters(const std::string & s) {
  // TODO: we should be counting utf-8 characters
  // but just over-estimate for now
  return s.size();
}

}

#endif

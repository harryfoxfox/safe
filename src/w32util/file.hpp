/*
  Safe: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef __Safe__windows_file_hpp
#define __Safe__windows_file_hpp

#include <w32util/error.hpp>

#include <memory>
#include <string>
#include <streambuf>

#include <cstddef>

#include <windows.h>

namespace w32util {

struct Buffer {
  std::unique_ptr<uint8_t[]> ptr;
  size_t size;
};

std::string
get_temp_path();

std::string
get_temp_file_name(std::string root, std::string prefix);

Buffer
read_file(HANDLE handle, DWORD num_bytes_to_read);

bool
file_exists(std::string file_path);

bool
map_to_same_target(std::string a, std::string b);

bool
ensure_deleted(std::string path);

std::string
basename(std::string path);

// NB: we use a custom stream instead of fstream
// because fstream is based on the CRT and that doesn't
// handle unicode file names properly
template <size_t BUF_SIZE>
class HandleInputStreamBuf : public std::streambuf {
  HANDLE _h;
  typename HandleInputStreamBuf::char_type _buf[BUF_SIZE];

public:
  HandleInputStreamBuf(HANDLE h) : _h(h) {
    setg(_buf, _buf, _buf);
  }

  int
  underflow() {
    if (gptr() == egptr()) {
      DWORD amt_read;
      auto success = ReadFile(_h, _buf,
                              sizeof(_buf), &amt_read,
                              nullptr);
      if (success) setg(_buf, _buf, _buf + amt_read);
    }

    return gptr() == egptr()
      ? HandleInputStreamBuf::traits_type::eof()
      : HandleInputStreamBuf::traits_type::to_int_type(*gptr());
  };
};

template <size_t BUF_SIZE>
class HandleOutputStreamBuf : public std::streambuf {
  HANDLE _h;
  typename HandleOutputStreamBuf::char_type _buf[BUF_SIZE];

public:
  HandleOutputStreamBuf(HANDLE h) : _h(h) {
    setp(_buf, _buf + sizeof(_buf) - 1);
  }

  ~HandleOutputStreamBuf() {
    // TODO: log error
    sync();
  }

  int
  overflow(std::streambuf::int_type ch) {
    auto end = pptr();
    if (!std::streambuf::traits_type::eq_int_type(ch, std::streambuf::traits_type::eof())) {
      *end++ = ch;
    }

    auto towrite = end - pbase();
    size_t total_written = 0;
    while (total_written != towrite) {
      DWORD amt_written;
      auto success = WriteFile(_h, pbase() + total_written,
                               towrite - total_written, &amt_written,
                               nullptr);
      if (!success) return std::streambuf::traits_type::eof();
      total_written += amt_written;
    }
    setp(_buf, _buf + sizeof(_buf) - 1);
    return 0;
  }

  int
  sync() {
    return overflow(std::streambuf::traits_type::eof()) ? -1 : 0;
  }
};

}

#endif

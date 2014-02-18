/*
  Safe: Encrypted File System
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

#ifndef _safe_parse_hpp
#define _safe_parse_hpp

#include <encfs/base/optional.h>

#include <cstddef>
#include <cstdint>

#include <stdexcept>
#include <string>
#include <vector>

namespace safe {

inline
void
skip_byte(size_t & fp, uint8_t *buf, size_t buf_size, uint8_t byte_) {
  while (fp < buf_size) {
    auto inb = buf[fp];
    if (inb != byte_) break;
    fp += 1;
  }
}

inline
std::string
parse_string_until_byte(size_t & fp,
                        uint8_t *buf, size_t buf_size,
                        uint8_t byte_) {
  if (fp >= buf_size) throw std::runtime_error("no string!");

  std::vector<char> build;
  while (fp < buf_size) {
    auto inb = buf[fp];
    if (inb == byte_) break;
    build.push_back(inb);
    fp += 1;
  }

  return std::string(build.begin(), build.end());
}

inline
void
expect(size_t & fp,
       uint8_t *buf, size_t buf_size,
       uint8_t byte_) {
  if (fp >= buf_size || buf[fp] != byte_) {
    throw std::runtime_error("Failed expect");
  }
  fp += 1;
}

inline
opt::optional<int8_t>
ascii_digit_value(uint8_t v) {
  const auto ASCII_0 = 48;
  const auto ASCII_9 = 57;
  if (v >= ASCII_0 && v <= ASCII_9) {
    return opt::make_optional((int8_t) (v - ASCII_0));
  }
  return opt::nullopt;
}


template <class IntegerType>
IntegerType
parse_ascii_integer(size_t & fp,
                    uint8_t *buf, size_t buf_size) {
  if (fp >= buf_size) {
    throw std::runtime_error("eof");
  }

  if (!ascii_digit_value(buf[fp])) {
    throw std::runtime_error("not at a number!");
  }

  IntegerType toret = 0;
  while (fp < buf_size) {
    auto inb = buf[fp];
    auto digit_val = ascii_digit_value(inb);
    if (!digit_val) break;

    auto oldtoret = toret;
    toret = toret * 10 + *digit_val;
    if (toret < oldtoret) {
      throw std::runtime_error("overflow occured");
    }

    fp += 1;
  }

  return toret;
}

// TODO: would be nice to generalize this to use c++ streams
class BufferParser {
  size_t _pos;
  uint8_t *_ptr;
  size_t _size;

public:
  BufferParser(uint8_t *ptr, size_t size)
    : _pos(0)
    , _ptr(ptr)
    , _size(size) {}

  void
  skip_byte(uint8_t byte_) {
    return safe::skip_byte(_pos, _ptr, _size, byte_);
  }

  std::string
  parse_string_until_byte(uint8_t byte_) {
    return safe::parse_string_until_byte(_pos, _ptr, _size, byte_);
  }

  void
  expect(uint8_t byte_)  {
    return safe::expect(_pos, _ptr, _size, byte_);
  }

  template <class IntegerType>
  IntegerType
  parse_ascii_integer() {
    return safe::parse_ascii_integer<IntegerType>(_pos, _ptr, _size);
  }
};

}

#endif

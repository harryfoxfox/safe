/*
 Safe: Encrypted File System
 Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

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

#include <safe/open_url.hpp>

// yes i know defines are suboptimal, but it's mostly contained
#ifdef __APPLE__
#include <safe/mac/util.hpp>
#define platform mac
#elif _WIN32
#include <safe/win/util.hpp>
#define platform win
#else
#error report_exception not supported on this platform
#endif

#include <iostream>
#include <sstream>
#include <unordered_set>

namespace safe {

template <class Integer>
static
char
to_ascii_hex(Integer a) {
  static const auto ASCII_A = 0x41;
  static const auto ASCII_0 = 0x30;
  if (a > 15) throw std::runtime_error("invalid character");
  else if (a > 9) return (char) ((a - 10) + ASCII_A);
  else return (char) (a + ASCII_0);
}

static
std::string
percent_encode(uint8_t a) {
  return std::string("%") + to_ascii_hex(a >> 4) + to_ascii_hex(a & 0xF);
}

static
bool
is_unreserved(char c) {
  static const std::unordered_set<char> mark = {'-', '_', '.', '!', '~', '*', '\'', '(', ')'};
  return ((c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') ||
          mark.count(c));
}

static
std::string
qarg_escape(const std::string & arg) {
  std::ostringstream os;

  // we treat all std::string as UTF-8 encoded
  for (const auto & c : arg) {
    if (is_unreserved(c)) os << c;
    else os << percent_encode((uint8_t) c);
  }

  return os.str();
}

void
open_url(const std::string & escaped_scheme_authority_path,
         const URLQueryArgs & unescaped_args) {
  std::ostringstream os;

  os << escaped_scheme_authority_path;

  if (!unescaped_args.empty()) os << "?";

  for (const auto & arg : unescaped_args) {
    os << qarg_escape(arg.name());
    if (arg.value()) os << "=" << qarg_escape(*arg.value());
    os << "&";
  }

  safe::platform::open_url(os.str());
}

}

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

#ifndef __safe_exception_backtrace_hpp
#define __safe_exception_backtrace_hpp

#include <safe/optional.hpp>

#include <exception>
#include <string>
#include <vector>

#include <cstdint>

namespace safe {

typedef std::vector<void *> Backtrace;
typedef std::vector<std::ptrdiff_t> OffsetBacktrace;

opt::optional<Backtrace>
backtrace_for_exception_ptr(std::exception_ptr p);

void
set_backtrace_for_exception_ptr(std::exception_ptr p, Backtrace backtrace);

void
_set_backtrace_for_exception_ptr(void *p, Backtrace backtrace);

struct ExceptionInfo {
  opt::optional<std::string> maybe_type_name;
  opt::optional<std::string> maybe_what;
  opt::optional<std::string> maybe_module;
  opt::optional<safe::OffsetBacktrace> maybe_offset_backtrace;
  std::string arch;
  std::string value_sizes;
};

ExceptionInfo
extract_exception_info(std::exception_ptr eptr);

class ExtraBinaryException : public std::exception {
  ExceptionInfo _einfo;

public:
  ExtraBinaryException(ExceptionInfo einfo)
    : _einfo(std::move(einfo)) {}

  // the "exception_info" name conflicts with something
  const ExceptionInfo &
  my_exception_info() const {
    return _einfo;
  }

  const char *
  what() const noexcept override {
    auto & maybe_what = my_exception_info().maybe_what;
    if (maybe_what) {
      return maybe_what->c_str();
    }
    else return "<unknown>";
  }
};

}

#endif

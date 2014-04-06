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

#include <safe/report_exception.hpp>

// yes i know defines are suboptimal, but it's mostly contained
#ifdef __APPLE__
#include <safe/mac/util.hpp>
#include <safe/mac/exception_backtrace.hpp>
#define platform mac
#elif _WIN32
#include <safe/win/util.hpp>
#include <safe/win/exception_backtrace.hpp>
#define platform win
#else
#error report_exception not supported on this platform
#endif

#include <safe/constants.h>
#include <safe/exception_backtrace.hpp>
#include <safe/open_url.hpp>
#include <safe/util.hpp>
#include <safe/version.h>

#include <typeinfo>

#include <cassert>

// for demangle()
#ifdef __GNUG__

#include <cstdlib>
#include <memory>
#include <cxxabi.h>

#endif


namespace safe {

const char *
exception_location_to_string(ExceptionLocation el) {
#define _CV(e) case e: return #e
  switch (el) {
    _CV(ExceptionLocation::SYSTEM_CHANGES);
    _CV(ExceptionLocation::STARTUP);
    _CV(ExceptionLocation::MOUNT);
    _CV(ExceptionLocation::CREATE);
    _CV(ExceptionLocation::TRAY_DISPATCH);
    _CV(ExceptionLocation::UNEXPECTED);
  default: /* notreached */ assert(false); return "";
  }
#undef _CV
}

#ifdef __GNUG__

std::string
demangle(const char *name) {

  int status = -4; // some arbitrary value to eliminate the compiler warning

  // enable c++11 by passing the flag -std=c++11 to g++
  auto res = std::unique_ptr<char, void (*)(void *)> {
    abi::__cxa_demangle(name, nullptr, nullptr, &status),
    std::free,
  };

  return !status ? res.get() : name;
}

#else

// does nothing if not g++
std::string
demangle(const char *name) {
  return name;
}

#endif

void
report_exception(ExceptionLocation el, std::exception_ptr eptr) {
  auto einfo = extract_exception_info(eptr);

  safe::URLQueryArgs qargs =
    {{"where", exception_location_to_string(el)},
     {"version", SAFE_VERSION_STR},
     {"target_platform", safe::platform::get_target_platform_tag()},
     {"platform", safe::platform::get_parseable_platform_version()},
     {"arch", einfo.arch},
     {"value_sizes", einfo.value_sizes}};

  if (einfo.maybe_module) {
    qargs.push_back({"module", std::move(*einfo.maybe_module)});
  }

  if (einfo.maybe_what) {
    qargs.push_back({"what", std::move(*einfo.maybe_what)});
  }

  if (einfo.maybe_type_name) {
    qargs.push_back({"exception_type", demangle(einfo.maybe_type_name->c_str())});
  }
          
  if (einfo.maybe_offset_backtrace) {
    auto ptr_to_hex_string = [] (ptrdiff_t ptr) {
      std::ostringstream os;
      os << std::showbase << std::hex << ptr;
      return os.str();
    };

    qargs.push_back({"offset_stack_trace",
          safe::join(",", safe::range_map(ptr_to_hex_string,
                                          *einfo.maybe_offset_backtrace))
          });
  }

  safe::open_url(SAFE_REPORT_EXCEPTION_WEBSITE, qargs);
}

}

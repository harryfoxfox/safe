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
#define platform mac
#elif _WIN32
#include <safe/win/util.hpp>
#define platform win
#else
#error report_exception not supported on this platform
#endif

#include <safe/constants.h>
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

static
std::string
get_target_fundamental_value_sizes() {
  auto to_str = [] (size_t a) {
    std::ostringstream os; os << a; return os.str();
  };

  return safe::join
    (",",
     safe::range_map
     (to_str, std::vector<size_t>
      {
        sizeof(char),
        sizeof(short),
        sizeof(int),
        sizeof(long),
        sizeof(long long),
        sizeof(void *),
        sizeof(float),
        sizeof(double),
        sizeof(long double),
      }
      )
     );
}

static
const char *
get_target_arch_tag() {
#if defined(__amd64__) || defined(_M_AMD64)
  return "amd64";
#elif defined(__i386__) || defined(_M_IX86)
  return "i386";
#else
#error "target abi identification not supported!"
#endif
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

template <class T>
std::string
type(const T & t) {
  return demangle(typeid(t).name());
}

void
report_exception(ExceptionLocation el, std::exception_ptr eptr,
                 opt::optional<std::vector<void *> > maybe_stack_trace) {
  safe::URLQueryArgs qargs =
    {{"where", exception_location_to_string(el)},
     {"arch", get_target_arch_tag()},
     {"version", SAFE_VERSION_STR},
     {"target_platform", safe::platform::get_target_platform_tag()},
     {"platform", safe::platform::get_parseable_platform_version()},
     {"value_sizes", get_target_fundamental_value_sizes()}};

  try {
    std::rethrow_exception(eptr);
  }
  catch (const std::exception & err) {
    qargs.push_back({"what", err.what()});
    // NB: type()/rtti requires polymorphic (virtual) types
    //     this works because std::exception is virtual
    qargs.push_back({"exception_type", type(err)});
  }
  catch (...) {
    // this exception isn't based on std::exception
    // dont send any info up
  }

  if (maybe_stack_trace) {
    auto & stack_trace = *maybe_stack_trace;
    auto ptr_to_hex_string = [] (void *ptr) {
      std::ostringstream os;
      os << std::hex << ptr;
      return os.str();
    };

    qargs.push_back({"stack_trace",
          safe::join(",", safe::range_map(ptr_to_hex_string, stack_trace))
          });
  }

  safe::open_url(SAFE_REPORT_EXCEPTION_WEBSITE, qargs);
}

}

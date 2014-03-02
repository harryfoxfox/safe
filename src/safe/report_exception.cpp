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
#include <safe/version.h>

#include <cassert>

namespace safe {

const char *
exception_location_to_string(ExceptionLocation el) {
#define _CV(e) case e: return #e
  switch (el) {
    _CV(ExceptionLocation::SYSTEM_CHANGES);
    _CV(ExceptionLocation::STARTUP);
    _CV(ExceptionLocation::MOUNT);
    _CV(ExceptionLocation::CREATE);
  default: /* notreached */ assert(false); return "";
  }
#undef _CV
}

static
const char *
get_target_abi_tag() {
#if defined(__amd64__) || defined(_M_AMD64)
  return "amd64";
#elif defined(__i386__) || defined(_M_IX86)
  return "i386";
#else
#error "target abi identification not supported!"
#endif
}

void
report_exception(ExceptionLocation el, std::exception_ptr eptr) {
  std::string what;
  try {
    std::rethrow_exception(eptr);
  }
  catch (const std::exception & err) {
    what = err.what();
  }

  safe::URLQueryArgs qargs =
    {{"where", exception_location_to_string(el)},
     {"what", what},
     {"version", SAFE_VERSION_STR},
     {"platform", safe::platform::get_parseable_platform_version()},
     {"abi", get_target_abi_tag()}};

  safe::open_url(SAFE_REPORT_EXCEPTION_WEBSITE, qargs);
}

}

/*
 Safe: Encrypted File System
 Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

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

#include <safe/report_exception.hpp>

#include <safe/constants.h>
#include <safe/open_url.hpp>

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
     {"what", what}};

  safe::open_url(SAFE_REPORT_EXCEPTION_WEBSITE, qargs);
}

}

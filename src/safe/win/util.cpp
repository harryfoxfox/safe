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

#include <safe/win/util.hpp>

#include <safe/util.hpp>

#include <w32util/error.hpp>
#include <w32util/gui_util.hpp>

#include <sstream>

#include <windows.h>

namespace safe { namespace win {

void
open_url(const std::string & url) {
  w32util::open_url_in_browser(nullptr, url);
}

static
bool
is_64_bit_windows() {
#ifdef _WIN64
  return true;
#else
  BOOL is_wow64_process;
  w32util::check_bool(IsWow64Process,
		      GetCurrentProcess(), &is_wow64_process);
  return is_wow64_process;
#endif
}

std::string
get_parseable_platform_version() {
  OSVERSIONINFOW vi;
  safe::zero_object(vi);
  vi.dwOSVersionInfoSize = sizeof(vi);
  w32util::check_bool(GetVersionEx, &vi);

  std::ostringstream os;

  os << "windows-" <<
    (is_64_bit_windows() ? "x64" : "x86") << "-" <<
    vi.dwMajorVersion  << "." <<
    vi.dwMinorVersion << "." <<
    vi.dwBuildNumber;

  return os.str();
}

bool
running_on_winxp() {
  OSVERSIONINFOW vi;
  safe::zero_object(vi);
  vi.dwOSVersionInfoSize = sizeof(vi);
  w32util::check_bool(GetVersionEx, &vi);
  return vi.dwMajorVersion < 6;
}

}}


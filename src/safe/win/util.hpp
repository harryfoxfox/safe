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

#ifndef __Safe__common_win
#define __Safe__common_win

#include <safe/util.hpp>
#include <w32util/error.hpp>
#include <w32util/string.hpp>
#include <w32util/gui_util.hpp>

#include <stdexcept>
#include <string>

#include <safe/lean_windows.h>
#include <shellapi.h>

namespace safe { namespace win {

namespace _int {

struct CloseHandleDeleter {
  void operator()(HANDLE a) {
    auto ret = CloseHandle(a);
    if (!ret) throw std::runtime_error("couldn't free!");
  }
};

}

typedef ManagedResource<HANDLE, _int::CloseHandleDeleter> _ManagedHandleBase;

class ManagedHandle : public _ManagedHandleBase {
public:
  using _ManagedHandleBase::_ManagedHandleBase;

  operator bool() const {
    return (bool) get();
  }
};

inline
DWORD
run_command_sync(std::string binary_path,
                   std::string parameters) {
  auto binary_path_w = w32util::widen(binary_path);
  auto parameters_w = w32util::widen(parameters);

  SHELLEXECUTEINFOW shex;
  safe::zero_object(shex);
  shex.cbSize = sizeof(shex);
  shex.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
  shex.lpVerb = L"open";
  shex.lpFile = binary_path_w.c_str();
  shex.lpParameters = parameters_w.c_str();
  shex.nShow = SW_HIDE;
  
  w32util::check_bool(ShellExecuteExW, &shex);

  if (!shex.hProcess) w32util::throw_windows_error();
  auto _close_process_handle =
    safe::create_deferred(CloseHandle, shex.hProcess);

  w32util::check_call(WAIT_FAILED, WaitForSingleObject,
                      shex.hProcess, INFINITE);

  DWORD exit_code;
  w32util::check_bool(GetExitCodeProcess, shex.hProcess, &exit_code);

  return exit_code;
}

void
open_url(const std::string & url);

std::string
get_parseable_platform_version();

bool
running_on_winxp();

bool
running_on_vista();

inline
const char *
get_target_platform_tag() {
  return "win";
}

}}


#endif

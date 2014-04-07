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

#include <safe/win/exception_backtrace.hpp>

#include <safe/exception_backtrace.hpp>
#include <safe/optional.hpp>
#include <safe/util.hpp>
#include <safe/win/util.hpp>

#include <w32util/sync.hpp>

#include <iostream>
#include <vector>
#include <unordered_map>

#include <windows.h>

// NB: Warning: this file is highly platform dependent
//     it requires a specific:
//     * Linker (GNU ld)
//     * C++ STL (libstdc++)
//     * Arch (i386 or x86_64)

namespace safe { namespace win {

void *
extract_raw_exception_pointer(std::exception_ptr p) {
  void *out;
  static_assert(sizeof(p) == sizeof(out), "exception ptr is not a pointer?");
  memcpy(&out, &p, sizeof(out));
  return out;
}

template <class T>
ptrdiff_t
pointer_difference_in_bytes(T *a, T *b) {
  return (uint8_t *) a - (uint8_t *) b;
}

void *
get_image_base() {
  HMODULE exe_module;
  w32util::check_bool(GetModuleHandleExW,
                      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCWSTR) &get_image_base,
                      &exe_module);
  return (void *) exe_module;
}

OffsetBacktrace
backtrace_to_offset_backtrace(const Backtrace & backtrace) {
  HMODULE exe_module;
  auto success = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCWSTR) &backtrace_to_offset_backtrace,
                                    &exe_module);
  if (!success) abort();

  std::vector<ptrdiff_t> stack_trace;
  for (const auto & addr : backtrace) {
    // find module of function
    HMODULE return_addr_module;
    auto success2 = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       // NB: we subtract one because the return address might be after the
                                       //     last byte in the module (remote chance of this happening for noreturn functions)
                                       (LPCWSTR) ((char *) addr - 1),
                                       &return_addr_module);

    stack_trace.push_back(!success2
                          ? (ptrdiff_t) -1
                          : return_addr_module == exe_module
                          ? pointer_difference_in_bytes(addr, (decltype(addr)) exe_module)
                          : 0);
  }

  return stack_trace;
}

}}

// NB: we use GNU ld's --wrap option to wrap the call to __cxa_throw()

extern "C"
void __real___cxa_throw(void *thrown_exception,
                        std::type_info *tinfo, void (*dest)(void *))
  __attribute__((noreturn));

extern "C"
void __wrap___cxa_throw(void *thrown_exception,
                        std::type_info *tinfo, void (*dest)(void *))
  __attribute__((noreturn));

class TlsBool {
  DWORD _tls_idx;
public:
  TlsBool() {
    _tls_idx = TlsAlloc();
    if (_tls_idx == TLS_OUT_OF_INDEXES) abort();
  }

  TlsBool &operator=(bool f) {
    auto success = TlsSetValue(_tls_idx, (LPVOID) f);
    if (!success) abort();
    return *this;
  }

  operator bool() const {
    auto ret = TlsGetValue(_tls_idx);
    if (!ret && GetLastError() != ERROR_SUCCESS) abort();
    return (bool) ret;
  }
};

void *_dummy = nullptr;

extern "C"
void
__wrap___cxa_throw(void *thrown_exception,
                   std::type_info *tinfo, void (*dest)(void *)) {
  static TlsBool is_currently_throwing;

  // if we throw during the course of executing stack save code
  // then we just abort
  if (is_currently_throwing) abort();

  is_currently_throwing = true;

  // NB: by using __builtin_frame_address() we force this function to have a
  //     frame pointer
  _dummy = __builtin_frame_address(0);

  // get stack trace
  void *frames[256];
  auto frames_captured =
    CaptureStackBackTrace(1, safe::numelementsf(frames),
                          frames, nullptr);

  auto stack_trace = std::vector<void *>(&frames[0],
                                         &frames[frames_captured]);

  safe::_set_backtrace_for_exception_ptr(thrown_exception, std::move(stack_trace));

  is_currently_throwing = false;

  // call original __cxa_throw
  __real___cxa_throw(thrown_exception, tinfo, dest);
}

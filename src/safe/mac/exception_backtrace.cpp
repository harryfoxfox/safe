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

#include <safe/mac/exception_backtrace.hpp>

#include <safe/exception_backtrace.hpp>
#include <safe/util.hpp>

#include <system_error>
#include <vector>

#include <cassert>
#include <cstdlib>

#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>

// NB: we wrap __cxa_throw to record last backtrace

// (for this to work, the C++ runtime library must be linked dynamically
//  if linked statically, look into using GNU ld "--wrap" option)

extern "C"
void
__cxa_throw(void *thrown_exception,
            std::type_info *tinfo, void (*dest)(void *)) __attribute__((noreturn));

typedef void (*cxa_throw_fn_t)(void *, std::type_info *, void (*)(void *))  __attribute__((__noreturn__));

static
cxa_throw_fn_t
get_original_cxa_throw() {
  static auto original_cxa_throw = (cxa_throw_fn_t) dlsym(RTLD_NEXT, "__cxa_throw");
  if (!original_cxa_throw) std::abort();
  return original_cxa_throw;
}

class TlsBool {
  pthread_key_t _key;

public:
  TlsBool() {
    pthread_key_create(&_key, nullptr);
  }

  ~TlsBool() {
    // TODO: log error
    pthread_key_delete(_key);
  }

  TlsBool &
  operator=(bool new_val) {
    auto ret2 = pthread_setspecific(_key, (void *) new_val);
    if (ret2) throw std::system_error(ret2, std::generic_category());
    return *this;
  }

  operator bool() const {
    return (bool) pthread_getspecific(_key);
  }
};

void *_dummy = nullptr;

extern "C"
void
__cxa_throw(void *thrown_exception,
            std::type_info *tinfo, void (*dest)(void *)) {
    static TlsBool is_currently_running;

    if (is_currently_running) std::abort();

    is_currently_running = true;

    // NB: by using __builtin_frame_address() we force this function to have a
    //     frame pointer
    _dummy = __builtin_frame_address(0);

    // get stack trace
    void *stack_trace[4096];
    auto addresses_written = backtrace(stack_trace, safe::numelementsf(stack_trace));

    safe::_set_backtrace_for_exception_ptr(thrown_exception,
                                           safe::Backtrace(&stack_trace[1], &stack_trace[addresses_written]));

    auto original_cxa_throw = get_original_cxa_throw();

    is_currently_running = false;

    original_cxa_throw(thrown_exception, tinfo, dest);
}

namespace safe { namespace mac {

void *
extract_raw_exception_pointer(std::exception_ptr p) {
  void *out;
  static_assert(sizeof(p) == sizeof(out), "exception ptr is not a pointer?");
  memcpy(&out, &p, sizeof(out));
  return out;
}

OffsetBacktrace
backtrace_to_offset_backtrace(const Backtrace & backtrace) {
    // figure out our base address
    Dl_info dlinfo;
    auto ret = dladdr((void *) &backtrace_to_offset_backtrace, &dlinfo);
    // NB: this should never happen
    if (!ret) abort();
    auto base_address = dlinfo.dli_fbase;

    std::vector<ptrdiff_t> offset_backtrace;
    for (const auto & addr : backtrace) {
        Dl_info dlinfo2;
        // NB: we subtract by one since addr points to the instruction
        //     after the call instruction and that could be the end of the function
        //     (in no-return functions)
        assert(addr);
        auto ret2 = dladdr((char *) addr - 1, &dlinfo2);
        offset_backtrace.push_back(!ret2
                                   ? -1
                                   : base_address == dlinfo2.dli_fbase
                                   ? (char *) addr - (char *) base_address
                                   : 0);
    }

    return offset_backtrace;
}

}}

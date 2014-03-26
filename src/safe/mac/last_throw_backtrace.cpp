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

#include <safe/mac/last_throw_backtrace.hpp>

#include <safe/optional.hpp>
#include <safe/util.hpp>

#include <typeinfo>
#include <vector>

#include <cstdlib>

#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>

// we wrap __cxa_throw to record last backtrace
static pthread_once_t _g_last_backtrace_init_control = PTHREAD_ONCE_INIT;
static pthread_key_t _g_last_backtrace_key;

static
void
destroy_backtrace(void *a) {
    if (a) delete (std::vector<void *> *) a;
}

static
void
init_last_backtrace_key() {
    pthread_key_create(&_g_last_backtrace_key, destroy_backtrace);
}

// NB: for this to work, the C++ runtime library must be linked dynamically
//     if linked statically, look into using GNU ld "--wrap" option

extern "C"
void
__cxa_throw(void *thrown_exception,
            std::type_info *tinfo, void (*dest)(void *)) __attribute__((noreturn));

extern "C"
void
__cxa_throw(void *thrown_exception,
            std::type_info *tinfo, void (*dest)(void *)) {
    auto ret = pthread_once(&_g_last_backtrace_init_control, init_last_backtrace_key);
    if (ret) std::abort();

    {
        // clear TLS stack trace
        destroy_backtrace(pthread_getspecific(_g_last_backtrace_key));

        // get stack trace
        void *stack_trace[4096];
        auto addresses_written = backtrace(stack_trace, safe::numelementsf(stack_trace));

        // save stack trace to TLS
        auto ret2 = pthread_setspecific(_g_last_backtrace_key, new std::vector<void *>(&stack_trace[0], &stack_trace[addresses_written]));
        if (ret2) std::abort();
    }

    typedef void (*cxa_throw_fn_t)(void *, std::type_info *, void (*)(void *))  __attribute__((__noreturn__));

    cxa_throw_fn_t original_cxa_throw = (cxa_throw_fn_t) dlsym(RTLD_NEXT, "__cxa_throw");
    if (!original_cxa_throw) std::abort();

    original_cxa_throw(thrown_exception, tinfo, dest);
}

namespace safe { namespace mac {

opt::optional<Backtrace>
last_throw_backtrace() {
    auto ret = pthread_once(&_g_last_backtrace_init_control, init_last_backtrace_key);
    if (ret) abort();

    auto current_backtrace_p = (std::vector<void *> *) pthread_getspecific(_g_last_backtrace_key);
    if (!current_backtrace_p) return opt::nullopt;

    return *current_backtrace_p;
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

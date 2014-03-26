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

#include <safe/win/last_throw_backtrace.hpp>

#include <safe/optional.hpp>
#include <safe/util.hpp>
#include <safe/win/util.hpp>

#include <w32util/sync.hpp>

#include <iostream>
#include <vector>
#include <unordered_map>

#include <windows.h>
#include <dbghelp.h>

// NB: We have to re-implement TLS on windows because
//     TlsAlloc() has no way of registering a "destroyer"
//     when the thread dies

// TODO: would be nice to abstract this tls functionality out

struct TlsMgmtState {
  struct ThreadInfo {
    safe::win::Backtrace backtrace;
    safe::win::ManagedHandle thread_handle;
  };

  w32util::CriticalSection mutex;
  safe::win::ManagedHandle cleanup_thread_handle;
  std::unordered_map<DWORD, ThreadInfo> thread_map;
  w32util::Event new_thread_update_event;

  bool
  can_watch_another_thread() const {
    return thread_map.size() < MAXIMUM_WAIT_OBJECTS - 1;
  }
};

WINAPI
static
DWORD
cleanup_thread_fn(LPVOID params_);

static
safe::win::ManagedHandle
_start_cleanup_thread() {
  auto unmanaged_cleanup_thread_handle = w32util::check_null(CreateThread,
                                                             nullptr, 0, cleanup_thread_fn,
                                                             nullptr, 0, nullptr);
  return safe::win::ManagedHandle(unmanaged_cleanup_thread_handle);
}

static
TlsMgmtState &
_get_tls_mgmt_state() {
  // lazy singleton init using C++11 thread-safe static initialization
  static TlsMgmtState state = {
    {},
    _start_cleanup_thread(),
    {},
    {false},
  };
  return state;
}

WINAPI
static
DWORD
cleanup_thread_fn(LPVOID params_) {
  (void) params_;

  auto & tls_mgmt_state = _get_tls_mgmt_state();

  while (true) {
    HANDLE wait_on[MAXIMUM_WAIT_OBJECTS];
    size_t num_waiters = 0;

    // always wait on the wakeup event
    wait_on[num_waiters++] = tls_mgmt_state.new_thread_update_event.get_handle();

    // get all threads we're waiting on to die
    {
      auto guard = tls_mgmt_state.mutex.create_guard();

      assert(tls_mgmt_state.thread_map.size() <= safe::numelementsf(wait_on) - num_waiters);
      for (const auto & tip : tls_mgmt_state.thread_map) {
        wait_on[num_waiters++] = tip.second.thread_handle.get();
      }
    }

    auto signaled = w32util::check_call(WAIT_FAILED,
                                        WaitForMultipleObjects,
                                        num_waiters, wait_on, FALSE, INFINITE);
    auto offset = signaled - WAIT_OBJECT_0;

    if (!offset) {
      // got the wakeup event, just spin
      continue;
    }
    else {
      // a thread died, update our state
      auto guard = tls_mgmt_state.mutex.create_guard();

      auto it = tls_mgmt_state.thread_map.begin();
      while (it != tls_mgmt_state.thread_map.end()) {
        if (it->second.thread_handle.get() == wait_on[offset]) {
          tls_mgmt_state.thread_map.erase(it);
          break;
        }
      }
    }
  }

  return 0;
}

static
void
_set_tls_backtrace(safe::win::Backtrace backtrace) noexcept {
  auto & tls_mgmt_state = _get_tls_mgmt_state();

  auto thread_handle_list_guard = tls_mgmt_state.mutex.create_guard();

  auto cur_thread_id = GetCurrentThreadId();

  auto thread_state_it = tls_mgmt_state.thread_map.find(cur_thread_id);

  if (thread_state_it != tls_mgmt_state.thread_map.end()) {
    thread_state_it->second.backtrace = std::move(backtrace);
  }
  else {
    if (!tls_mgmt_state.can_watch_another_thread()) {
      throw std::runtime_error("Cannot allocate another TLS block");
    }

    HANDLE new_thread_handle;
    w32util::check_bool(DuplicateHandle,
                        GetCurrentProcess(),
                        GetCurrentThread(),
                        GetCurrentProcess(),
                        &new_thread_handle,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

    auto managed_thread_handle = safe::win::ManagedHandle(new_thread_handle);

    tls_mgmt_state.thread_map[cur_thread_id] = {
      std::move(backtrace),
      std::move(managed_thread_handle),
    };

    // we've modified the thread list, notify the thread watcher
    tls_mgmt_state.new_thread_update_event.set();
  }
}

static
opt::optional<safe::win::Backtrace>
_get_tls_backtrace() {
  auto & tls_mgmt_state = _get_tls_mgmt_state();

  auto thread_handle_list_guard = tls_mgmt_state.mutex.create_guard();

  auto cur_thread_id = GetCurrentThreadId();

  auto it = tls_mgmt_state.thread_map.find(cur_thread_id);

  return it == tls_mgmt_state.thread_map.end()
    ? opt::nullopt
    : opt::make_optional(it->second.backtrace);
}

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
  auto cur_bp = __builtin_frame_address(0);

  // derive pseudo-processor state after call to __cxa_throw()
#if defined(__i386__) || defined(__x86_64)
  auto bp_before_our_call = *(void **) cur_bp;
  auto sp_before_our_call = (void *) (((intptr_t) cur_bp) + 2 * sizeof(void *));
  auto return_address = *(void **) ((intptr_t) cur_bp + sizeof(void *));
#else
#error arch not supported!
#endif

  CONTEXT ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.ContextFlags = CONTEXT_FULL;
  ctx.Eip = (DWORD) return_address;
  ctx.Esp = (DWORD) sp_before_our_call;
  ctx.Ebp = (DWORD) bp_before_our_call;

  // init stack frame structure
  STACKFRAME64 frame;
  memset(&frame, 0, sizeof(frame));

#if defined(__i386__) && !defined(__x86_64)
  frame.AddrPC.Offset = ctx.Eip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = ctx.Esp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = ctx.Ebp;
  frame.AddrFrame.Mode = AddrModeFlat;
  const auto machine = IMAGE_FILE_MACHINE_I386;
#elif defined(__x86_64)
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  const auto machine = IMAGE_FILE_MACHINE_AMD64;
#else
#error "arch not supported!"
#endif

  auto process = GetCurrentProcess();
  auto thread = GetCurrentThread();

  // get stack trace
  auto stack_trace = std::vector<void *>();
  while (true) {
    auto success = StackWalk64(machine, process, thread,
                               &frame, &ctx,
                               nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                               nullptr);
    if (!success) break;

    stack_trace.push_back((void *) frame.AddrPC.Offset);
  }

  _set_tls_backtrace(std::move(stack_trace));

  is_currently_throwing = false;

  // call original __cxa_throw
  __real___cxa_throw(thrown_exception, tinfo, dest);
}

namespace safe { namespace win {

opt::optional<Backtrace>
last_throw_backtrace() {
  return _get_tls_backtrace();
}

void
set_last_throw_backtrace(Backtrace bt) {
  _set_tls_backtrace(std::move(bt));
}

template <class T>
ptrdiff_t
pointer_difference_in_bytes(T *a, T *b) {
  return (uint8_t *) a - (uint8_t *) b;
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


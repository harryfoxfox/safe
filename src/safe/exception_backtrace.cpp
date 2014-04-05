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

#include <safe/exception_backtrace.hpp>

// yes i know defines are suboptimal, but it's mostly contained
#ifdef __APPLE__
#include <safe/mac/mutex.hpp>
#include <safe/mac/exception_backtrace.hpp>
#define platform mac
#elif _WIN32
#include <safe/win/mutex.hpp>
#include <safe/win/exception_backtrace.hpp>
#define platform win
#else
#error exception backtrace not supported on this platform
#endif

#include <safe/optional.hpp>

#include <algorithm>
#include <unordered_map>

namespace safe {

// NB: we use an LRU strategy to keep backtraces around
static size_t MAX_EPTRS = 30;

struct GlobalExceptionBacktraceState {
  typedef unsigned long long counter_type;

  struct _MapValue {
    safe::Backtrace backtrace;
    counter_type last_use;
  };

  safe::platform::Mutex mutex;
  std::unordered_map<void *, _MapValue> eptr_to_backtrace;
  counter_type usage_counter;
};

static
GlobalExceptionBacktraceState &
get_exception_backtrace_state() {
  static GlobalExceptionBacktraceState _state;
  return _state;
}

opt::optional<Backtrace>
backtrace_for_exception_ptr(std::exception_ptr p) {
  auto & g = get_exception_backtrace_state();

  auto guard = g.mutex.create_guard();

  auto raw_eptr = safe::platform::extract_raw_exception_pointer(p);

  auto it = g.eptr_to_backtrace.find(raw_eptr);
  if (it == g.eptr_to_backtrace.end()) {
    return opt::nullopt;
  }
  else {
    return it->second.backtrace;
  }
}

// meant for platform-specific __cxa_throw interposers
void
_set_backtrace_for_exception_ptr(void *raw_eptr, Backtrace backtrace) {
  auto & g = get_exception_backtrace_state();

  auto guard = g.mutex.create_guard();

  auto it = g.eptr_to_backtrace.find(raw_eptr);
  if (it == g.eptr_to_backtrace.end()) {
    if (g.eptr_to_backtrace.size() >= MAX_EPTRS) {
      // We have too many eptrs lying around
      // remove the oldest one
      typedef decltype(*g.eptr_to_backtrace.begin()) value_type;
      auto max_elt = std::min_element(g.eptr_to_backtrace.begin(),
                                      g.eptr_to_backtrace.end(),
                                      [] (const value_type & a,
                                          const value_type & b) {
                                        return a.second.last_use < b.second.last_use;
                                      });
      g.eptr_to_backtrace.erase(max_elt);
    }

    g.eptr_to_backtrace.insert({raw_eptr, {backtrace, 0}});
  }
  else {
    it->second = {backtrace, g.usage_counter};
  }

  g.usage_counter += 1;
}

void
set_backtrace_for_exception_ptr(std::exception_ptr p, Backtrace backtrace) {
  return _set_backtrace_for_exception_ptr(safe::platform::extract_raw_exception_pointer(p), std::move(backtrace));
}

}

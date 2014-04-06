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
#include <safe/util.hpp>

#include <algorithm>
#include <sstream>
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

ExceptionInfo
extract_exception_info(std::exception_ptr eptr) {
  bool is_extra_binary_exception = false;
  ExceptionInfo einfo;

  try {
    std::rethrow_exception(eptr);
  }
  catch (const safe::ExtraBinaryException & err) {
    einfo = err.my_exception_info();
    is_extra_binary_exception = true;
  }
  catch (const std::exception & err) {
    einfo.maybe_what = std::string(err.what());
    // NB: type()/rtti requires polymorphic (virtual) types
    //     this works because std::exception is virtual
    einfo.maybe_type_name = std::string(typeid(err).name());
  }
  catch (...) {
    // this exception isn't based on std::exception
    // dont send any info up
  }

  if (!is_extra_binary_exception) {
    einfo.arch = get_target_arch_tag();
    einfo.value_sizes = get_target_fundamental_value_sizes();

    // if this wasn't an external binary, get the backtrace from
    // the eptr
    auto maybe_backtrace = safe::backtrace_for_exception_ptr(eptr);
    if (maybe_backtrace) {
      einfo.maybe_offset_backtrace = safe::platform::backtrace_to_offset_backtrace(std::move(*maybe_backtrace));
    }
  }

  return einfo;
}

}

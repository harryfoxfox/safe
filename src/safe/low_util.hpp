#ifndef _safe_low_util_hpp
#define _safe_low_util_hpp

#include <cstddef>

namespace safe {

template<typename T, size_t N>
constexpr size_t
numelementsf(T (&)[N]) {
  return N;
}

}

#endif

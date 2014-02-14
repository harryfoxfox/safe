#ifndef _lockbox_low_util_hpp
#define _lockbox_low_util_hpp

#include <cstddef>

namespace lockbox {

template<typename T, size_t N>
constexpr size_t
numelementsf(T (&)[N]) {
  return N;
}

}

#endif

#ifndef _lockbox_util_hpp
#define _lockbox_util_hpp

#include <lockbox/deferred.hpp>
#include <lockbox/logging.h>
#include <lockbox/low_util.hpp>

#include <encfs/base/optional.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <cassert>
#include <cstring>

namespace lockbox {

struct ErrorMessage {
  std::string title;
  std::string message;
};

inline
ErrorMessage
make_error_message(std::string title, std::string msg) {
  return ErrorMessage {std::move(title), std::move(msg)};
}

template <class T>
using CDestroyer = CDeferred<T>;

template <class Destroyer, class T>
CDestroyer<Destroyer(T)> create_destroyer(T t, Destroyer f) {
  return CDestroyer<Destroyer(T)>(std::move(f), std::move(t));
}

template <class T>
using DynamicManagedResource = CDestroyer<std::function<void(T)>(T)>;

template <class T, class F>
DynamicManagedResource<T> create_dynamic_managed_resource(T a, F f) {
  std::function<void(T)> f_ = [=] (T arg) {
    f(arg);
  };
  return DynamicManagedResource<T>(std::move(f_), std::move(a));
}

template <class T, class F>
class ManagedResource {
  struct SubF {
    void operator()(T *todel) noexcept {
      try {
        F()(*todel);
      }
      catch (...) {
        lbx_log_error("Failed to free resource, leaking...");
      }
      delete todel;
    }
  };

  std::shared_ptr<T> _ptr;

public:
  explicit ManagedResource(T arg_) : _ptr(new T(std::move(arg_)), SubF()) {}
  ManagedResource() : _ptr() {}

  const T & get() const {
    return *_ptr;
  }

  void reset(T arg_) {
    _ptr.reset(new T(std::move(arg_)), SubF());
  }
};

template <class T>
void
zero_object(T & obj) noexcept {
  memset(&obj, 0, sizeof(obj));
}

// TODO: conditional definition based on whether we are using a c++14 compatible library
template <class T, class ...Args>
std::unique_ptr<T>
make_unique(Args &&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

inline
std::string
escape_double_quotes(std::string mount_name) {
    std::vector<char> replaced;
    for (const auto & c : mount_name) {
        if (c == '"') {
            replaced.push_back('\\');
        }
        replaced.push_back(c);
    }
    return std::string(replaced.begin(), replaced.end());
}

namespace _int {

template<typename T,
         typename std::enable_if<std::is_integral<T>::value>::type * = nullptr>
class IntegralIterator {
private:
  T _val;

public:
  IntegralIterator(T start_val) : _val(std::move(start_val)) {}

  T & operator*() { return _val; }
  IntegralIterator & operator++() { ++_val; return *this; }
  IntegralIterator operator++(int) { return IntegralIterator(_val++); }
  IntegralIterator & operator--() { --_val; return *this; }
  IntegralIterator operator--(int) { return IntegralIterator(_val--); }

  bool operator==(const IntegralIterator & other) const {
    return _val == other._val;
  }

  bool operator!=(const IntegralIterator & other) const {
    return !(*this == other);
  }
};

template<typename T>
class Range {
private:
  T _max;

public:
  explicit Range(T max) : _max(std::move(max)) {}

  IntegralIterator<T> begin() const { return IntegralIterator<T>(0); }
  IntegralIterator<T> end() const { return IntegralIterator<T>(_max); }
};

template<typename Iterator>
class EnumerateIterator {
  typedef size_t size_type;

  size_type _idx;
  Iterator _iterator;

public:
  struct EnumerateContainer {
    size_type index;
    decltype(*std::declval<Iterator>()) value;
  };

  EnumerateIterator(size_type idx,
                    Iterator iterator)
    : _idx(std::move(idx))
    , _iterator(std::move(iterator)) {}

  EnumerateContainer operator*() {
    return {_idx, *_iterator};
  }

  EnumerateIterator & operator++() {
    ++_idx; ++_iterator;
    return *this;
  }
  EnumerateIterator operator++(int) {
    return EnumerateIterator(_idx++, _iterator++);
  }

  EnumerateIterator & operator--() {
    --_idx; --_iterator;
    return *this;
  }

  EnumerateIterator operator-(int a) {
    return EnumerateIterator(_idx - a, _iterator - a);    
  }
  
  EnumerateIterator operator--(int) {
    return EnumerateIterator(_idx--, _iterator--);
  }

  bool operator==(const EnumerateIterator & other) const {
    return _idx == other._idx && _iterator == other._iterator;
  }

  bool operator!=(const EnumerateIterator & other) const {
    return !(*this == other);
  }
};

template<typename RangeType>
class Enumerate {
  typedef decltype(std::declval<RangeType>().begin()) BeginIterator;
  typedef decltype(std::declval<RangeType>().end()) EndIterator;

  BeginIterator _begin;
  EndIterator _end;
  
public:
  template<typename R>
  explicit Enumerate(R && rt)
    : _begin(std::forward<R>(rt).begin())
    , _end(std::forward<R>(rt).end()) {}

  EnumerateIterator<BeginIterator>
  begin() const {
    return EnumerateIterator<BeginIterator>(0, _begin);
  }

  EnumerateIterator<EndIterator>
  end() const {
    return EnumerateIterator<EndIterator>(_end - _begin, _end);
  }
};

template<typename Iterator>
class ReversedIterator {
  Iterator _iterator;

public:
  ReversedIterator(Iterator iterator)
    : _iterator(std::move(iterator)) {}

  decltype(*(std::declval<Iterator>() - 1)) operator*() {
    return *(_iterator - 1);
  }

  ReversedIterator & operator++() {
    --_iterator;
    return *this;
  }
  ReversedIterator operator++(int) {
    return ReversedIterator(_iterator--);
  }

  ReversedIterator & operator--() {
    ++_iterator;
    return *this;
  }
  
  ReversedIterator operator--(int) {
    return ReversedIterator(_iterator++);
  }

  bool operator==(const ReversedIterator & other) const {
    return _iterator == other._iterator;
  }

  bool operator!=(const ReversedIterator & other) const {
    return !(*this == other);
  }
};

template<typename RangeType>
class Reversed {
  typedef decltype(std::declval<RangeType>().begin()) BeginIterator;
  typedef decltype(std::declval<RangeType>().end()) EndIterator;

  BeginIterator _begin;
  EndIterator _end;
  
public:
  template<typename R>
  explicit Reversed(R && rt)
    : _begin(std::forward<R>(rt).begin())
    , _end(std::forward<R>(rt).end()) {}

  ReversedIterator<EndIterator> begin() const {
    return ReversedIterator<EndIterator>(_end);
  }

  ReversedIterator<BeginIterator> end() const {
    return ReversedIterator<BeginIterator>(_begin);
  }
};

}

/* works like a python range(), e.g.
   for (auto & _ : range(4));
   <=>
   for _ in range(4): pass
*/
template<typename T>
_int::Range<T>
range(T max) {
  return _int::Range<T>(max);
}

template<typename RangeType>
_int::Enumerate<typename std::decay<RangeType>::type>
enumerate(RangeType && r) {
  return _int::Enumerate<typename std::decay<RangeType>::type>(std::forward<RangeType>(r));
}

template<typename RangeType>
_int::Reversed<typename std::decay<RangeType>::type>
reversed(RangeType && r) {
  return _int::Reversed<typename std::decay<RangeType>::type>(std::forward<RangeType>(r));
}

template<typename T>
struct numbits {
  static const size_t value = sizeof(T) * 8;
};

template<typename T>
constexpr
size_t
numbitsf(T) {
  return numbits<T>::value;
}

template<typename T>
typename std::enable_if<std::is_integral<T>::value, size_t>::type
position_of_highest_bit_set(T a) {
  assert(a);
  size_t toret = 0;
  while (a >>= 1) ++toret;
  return toret;
}

template<typename T>
T
create_bit_mask(size_t num_bits_to_mask) {
  assert(num_bits_to_mask <= numbits<T>::value);
  return ((T) 1 << num_bits_to_mask) - 1;
}

}

#endif

#ifndef _lockbox_util_hpp
#define _lockbox_util_hpp

#include <lockbox/logging.h>

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

// this little class allows us to get C++ RAII with C based data structures
template <class FNRET>
class CDestroyer;

// CDestroyer of one argument also serves a managed handle object
template <class F, class T>
class CDestroyer<F(T)> {
  F f;
  T data;
  bool is_valid;

public:
  CDestroyer()
    : is_valid(false) {}

  CDestroyer(F f_, T arg_)
    : f(std::move(f_))
    , data(std::move(arg_))
    , is_valid(true) {}

  CDestroyer(CDestroyer && cd)
    : f(std::move(cd.f))
    , data(std::move(cd.data))
    , is_valid(cd.is_valid) {
    cd.is_valid = false;
  }

  CDestroyer &operator=(CDestroyer && cd) {
    if (this != &cd) {
      this->~CDestroyer();
      new (this) CDestroyer(std::move(cd));
    }
    return *this;
  }

  ~CDestroyer() { if (is_valid) f(data); }
};

template <class F, class T, class V>
class CDestroyer<F(T,V)> {
  F f;
  T a1;
  V a2;
  bool is_valid;

public:
  CDestroyer(F f_, T t, V v)
    : f(f_)
    , a1(std::move(t))
    , a2(std::move(v))
    , is_valid(true) {}

  CDestroyer(CDestroyer && cd)
    : f(std::move(cd.f))
    , a1(std::move(cd.a1))
    , a2(std::move(cd.a2))
    , is_valid(cd.is_valid) {
    cd.is_valid = false;
  }

  CDestroyer &operator=(CDestroyer && cd) {
    if (this != &cd) {
      this->~CDestroyer();
      new (this) CDestroyer(std::move(cd));
    }
    return *this;
  }

  ~CDestroyer() { if (is_valid) f(a1, a2); }
};

template <class Destroyer, class T>
CDestroyer<Destroyer(T)> create_destroyer(T t, Destroyer f) {
  return CDestroyer<Destroyer(T)>(std::move(f), std::move(t));
}

template <class Destroyer, class... Args>
CDestroyer<Destroyer(Args...)> create_deferred(Destroyer f, Args ...args) {
  return CDestroyer<Destroyer(Args...)>(std::move(f), std::move(args)...);
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
  IntegralIterator operator++(int) const { return IntegralIterator(_val++); }
  IntegralIterator & operator--() { --_val; return *this; }
  IntegralIterator operator--(int) const { return IntegralIterator(_val--); }

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

template<typename T, size_t N>
constexpr size_t
numelementsf(T (&)[N]) {
  return N;
}

}

#endif

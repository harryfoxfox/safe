#ifndef _safe_util_hpp
#define _safe_util_hpp

#include <safe/deferred.hpp>
#include <safe/logging.h>
#include <safe/low_util.hpp>

#include <encfs/base/optional.h>

#include <algorithm>
#include <functional>
#include <numeric>
#include <memory>
#include <string>
#include <vector>

#include <cassert>
#include <cstring>

namespace safe {

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

  operator bool() const {
    return (bool) _ptr;
  }

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

inline
std::string
wrap_quotes(std::string str) {
  return "\"" + escape_double_quotes(str) + "\"";
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

  decltype(std::declval<Iterator>() - std::declval<Iterator>())
  operator-(const ReversedIterator & a) const {
    return a._iterator - _iterator;
  }
};

template<class F, class Iterator>
class MapIterator {
  F _fn;
  Iterator _iterator;

public:
  MapIterator(F fn, Iterator it)
  : _fn(std::move(fn))
  , _iterator(std::move(it))
  {}

  typename std::result_of<F(decltype(*std::declval<Iterator>()))>::type operator*() {
    return _fn(*_iterator);
  }

  MapIterator & operator++() {
    ++_iterator;
    return *this;
  }
  MapIterator operator++(int) {
    return MapIterator(_fn, _iterator++);
  }

  MapIterator & operator--() {
    --_iterator;
    return *this;
  }

  MapIterator operator--(int) {
    return MapIterator(_fn, _iterator--);
  }

  bool operator==(const MapIterator & other) const {
    // TODO: should we equate the function object? doesn't work for lambdas
    return _iterator == other._iterator;
  }

  bool operator!=(const MapIterator & other) const {
    return !(*this == other);
  }
};

template<class Iterator1, class Iterator2,
         class EndIterator1, class EndIterator2>
class ZipIterator {
  Iterator1 _it1;
  Iterator2 _it2;
  EndIterator1 _end1;
  EndIterator2 _end2;

public:
  ZipIterator(Iterator1 it1, Iterator2 it2,
              EndIterator1 end1, EndIterator2 end2)
    : _it1(std::move(it1))
    , _it2(std::move(it2))
    , _end1(std::move(end1))
    , _end2(std::move(end2))
  {}

  decltype(std::make_pair(*std::declval<Iterator1>(),
                          *std::declval<Iterator2>()))
  operator*() {
    return std::make_pair(*_it1, *_it2);
  }

  ZipIterator & operator++() {
    ++_it1;
    ++_it2;
    return *this;
  }

  ZipIterator operator++(int) {
    return ZipIterator(_it1++, _it2++, _end1, _end2);
  }

  ZipIterator & operator--() {
    --_it1;
    --_it2;
    return *this;
  }

  ZipIterator operator--(int) {
    return ZipIterator(_it1--, _it2--, _end1, _end2);
  }

  bool operator==(const ZipIterator & other) const {
    if (!(_end1 == other._end1 &&
          _end2 == other._end2)) return false;

    bool we_are_an_end_iterator = (_it1 == _end1 &&
                                   _it2 == _end2);
    bool they_are_an_end_iterator = (other._it1 == other._end1 &&
                                     other._it2 == other._end2);

    bool we_are_at_end = (_it1 == _end1 || _it2 == _end2);
    bool they_are_at_end = (other._it1 == other._end1 ||
                            other._it2 == other._end2);

    if (we_are_at_end && they_are_an_end_iterator) {
      return true;
    }
    else if (we_are_an_end_iterator && they_are_at_end) {
      return true;
    }
    else {
      return _it1 == other._it1 && _it2 == other._it2;
    }
  }

  bool operator!=(const ZipIterator & other) const {
    return !(*this == other);
  }

  decltype(std::declval<Iterator1>() - std::declval<Iterator1>())
  operator-(const ZipIterator & a) const {
    // we use the first iterator here,
    // since we co
    return _it1 - a._it1;
  }
};

template <class BeginIterator, class EndIterator>
class IteratorBasedRange {
    BeginIterator _begin;
    EndIterator _end;

public:
    template <class U, class V>
    IteratorBasedRange(U && begin, V && end)
    : _begin(std::forward<U>(begin)), _end(std::forward<V>(end)) {}

    BeginIterator
    begin() const { return _begin; }

    EndIterator
    end() const { return _end; }
};

}

template<class BeginIterator, class EndIterator>
_int::IteratorBasedRange<BeginIterator, EndIterator>
make_range(BeginIterator && begin, EndIterator && end) {
    return _int::IteratorBasedRange<typename std::decay<BeginIterator>::type,
                                    typename std::decay<EndIterator>::type>(std::forward<BeginIterator>(begin),
                                                                            std::forward<EndIterator>(end));
}

/* works like a python range(), e.g.
   for (auto & _ : range(4));
   <=>
   for _ in range(4): pass
*/
template<typename T>
auto
range(T max) ->
decltype(make_range(_int::IntegralIterator<T>(0), _int::IntegralIterator<T>(max))) {
  return make_range(_int::IntegralIterator<T>(0), _int::IntegralIterator<T>(max));
}

template<typename RangeType>
auto
enumerate(RangeType && r) ->
decltype(make_range(_int::EnumerateIterator<decltype(r.begin())>(0, r.begin()),
                    _int::EnumerateIterator<decltype(r.end())>(r.end() - r.begin(), r.end()))) {
  return make_range(_int::EnumerateIterator<decltype(r.begin())>(0, r.begin()),
                    _int::EnumerateIterator<decltype(r.end())>(r.end() - r.begin(), r.end()));
}

template<typename RangeType>
auto
reversed(RangeType && r) ->
decltype(make_range(_int::ReversedIterator<decltype(r.end())>(r.end()),
                    _int::ReversedIterator<decltype(r.begin())>(r.begin()))) {
  return make_range(_int::ReversedIterator<decltype(r.end())>(r.end()),
                    _int::ReversedIterator<decltype(r.begin())>(r.begin()));
}

template<typename F, typename RangeType>
auto
range_map(F && fn, RangeType && r) ->
decltype(make_range(_int::MapIterator<typename std::decay<F>::type, decltype(r.begin())>(std::forward<F>(fn), r.begin()),
                    _int::MapIterator<typename std::decay<F>::type, decltype(r.end())>(std::forward<F>(fn), r.end()))) {
    return make_range(_int::MapIterator<typename std::decay<F>::type, decltype(r.begin())>(std::forward<F>(fn), r.begin()),
                    _int::MapIterator<typename std::decay<F>::type, decltype(r.end())>(std::forward<F>(fn), r.end()));
}

template<typename RangeType1, typename RangeType2>
auto
range_zip(RangeType1 && r, RangeType2 && r2) ->
decltype(make_range(_int::ZipIterator<decltype(r.begin()), decltype(r2.begin()), decltype(r.end()), decltype(r2.end())>
                    (r.begin(), r2.begin(), r.end(), r2.end()),
                    _int::ZipIterator<decltype(r.end()), decltype(r2.end()), decltype(r.end()), decltype(r2.end())>
                    (r.end(), r2.end(), r.end(), r2.end()))) {
  // i implemented this because i was feeling particularly
  // masochistic / adventurous
    return make_range(_int::ZipIterator<decltype(r.begin()), decltype(r2.begin()), decltype(r.end()), decltype(r2.end())>
                      (r.begin(), r2.begin(), r.end(), r2.end()),
                      _int::ZipIterator<decltype(r.end()), decltype(r2.end()), decltype(r.end()), decltype(r2.end())>
                      (r.end(), r2.end(), r.end(), r2.end()));
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

template <class Range>
decltype(*std::declval<Range>().begin())
join(std::string joiner, const Range & strings) {
  auto it = strings.begin();
  if (it == strings.end()) return "";
  auto first = *it++;
  return std::accumulate(it, strings.end(), first,
                         [&] (const std::string & a, const std::string & b) {
                           return a + joiner + b;
                         });
}

}

#endif

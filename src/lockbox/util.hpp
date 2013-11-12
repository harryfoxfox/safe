#ifndef _lockbox_util_H
#define _lockbox_util_H

#include <encfs/base/optional.h>

#include <cstring>

namespace lockbox {

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
  T data;
  bool is_valid;

public:
  ManagedResource(T arg_)
    : data(std::move(arg_))
    , is_valid(true) {}

  ManagedResource(ManagedResource && f)
    : data(std::move(f.data))
    , is_valid(f.is_valid) {
    f.is_valid = false;
  }

  ManagedResource &operator=(ManagedResource && f) {
    if (this != &f) {
      this->~ManagedResource();
      new (this) ManagedResource(std::move(f));
    }
    return *this;
  }

  ~ManagedResource() { if (is_valid) F()(data); };

  const T & get() const {
    if (!is_valid) throw std::runtime_error("invalid resource!");
    return data;
  }
};

template <class T>
void
zero_object(T & obj) {
  memset(&obj, 0, sizeof(obj));
}

// TODO: conditional definition based on whether we are using a c++14 compatible library
template <class T, class ...Args>
std::unique_ptr<T>
make_unique(Args &&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

}

#endif

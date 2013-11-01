#ifndef _lockbox_util_H
#define _lockbox_util_H

#include <cstring>

namespace lockbox {

/* this little class allows us to get C++ RAII with C based data structures */
template <class FNRET>
class CDestroyer;

template <class F, class T>
class CDestroyer<F(T)> {
private:
  F f;
  T arg;
public:
  CDestroyer(F f_, T && arg_) : f(f_), arg(std::forward<T>(arg_)) {}
  ~CDestroyer() { f(arg);};
};

template <class F, class T, class V>
class CDestroyer<F(T,V)> {
private:
  F f;
  T a1;
  V a2;
public:
  CDestroyer(F f_, T && t, V && v)
    : f(f_)
    , a1(std::forward<T>(t))
    , a2(std::forward<V>(v)) {}
  ~CDestroyer() {
    f(a1, a2);
  }
};

template <class Destroyer, class T>
CDestroyer<Destroyer(T)> create_destroyer(T && t, Destroyer f) {
  return CDestroyer<Destroyer(T)>(f, std::forward<T>(t));
}

template <class Destroyer, class... Args>
CDestroyer<Destroyer(Args...)> create_deferred(Destroyer f, Args &&... args) {
  return CDestroyer<Destroyer(Args...)>(f, std::forward<Args>(args)...);
}

template <class T>
void
zero_object(T & obj) {
  memset(&obj, 0, sizeof(obj));
}

}

#endif

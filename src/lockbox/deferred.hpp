#ifndef _lockbox_deferred_hpp
#define _lockbox_deferred_hpp

namespace lockbox {

namespace _int {

template< class T > struct remove_reference      {typedef T type;};
template< class T > struct remove_reference<T&>  {typedef T type;};
template< class T > struct remove_reference<T&&> {typedef T type;};
template<class T> struct is_lvalue_reference {
  static const bool value = false;
};
template<class T> struct is_lvalue_reference<T&> {
  static const bool value = true;
};

template <class _Tp>
inline constexpr
typename remove_reference<_Tp>::type&&
move(_Tp&& __t) noexcept
{
    typedef typename remove_reference<_Tp>::type _Up;
    return static_cast<_Up&&>(__t);
}

template <class _Tp>
inline constexpr
_Tp&&
forward(typename remove_reference<_Tp>::type& __t) noexcept
{
    return static_cast<_Tp&&>(__t);
}

template <class _Tp>
inline
_Tp&&
forward(typename remove_reference<_Tp>::type&& __t) noexcept
{
    static_assert(!is_lvalue_reference<_Tp>::value,
                  "Can not forward an rvalue as an lvalue.");
    return static_cast<_Tp&&>(__t);
}  
}

// this little class allows us to get C++ RAII with C based data structures
template <class FNRET>
class CDeferred;

template <class F>
class CDeferred<F()> {
  F f;
  bool is_valid;

public:
  CDeferred()
    : is_valid(false) {}

  CDeferred(F f_)
    : f(_int::move(f_))
    , is_valid(true) {}

  CDeferred(CDeferred && cd)
    : f(_int::move(cd.f))
    , is_valid(cd.is_valid) {
    cd.is_valid = false;
  }

  CDeferred &operator=(CDeferred && cd) {
    if (this != &cd) {
      this->~CDeferred();
      new (this) CDeferred(_int::move(cd));
    }
    return *this;
  }

  void
  cancel() {
    is_valid = false;
  }

  ~CDeferred() { if (is_valid) f(); }
};

// CDeferred of one argument also serves a managed handle object
template <class F, class T>
class CDeferred<F(T)> {
  F f;
  T data;
  bool is_valid;

public:
  CDeferred()
    : is_valid(false) {}

  CDeferred(F f_, T arg_)
    : f(_int::move(f_))
    , data(_int::move(arg_))
    , is_valid(true) {}

  CDeferred(CDeferred && cd)
    : f(_int::move(cd.f))
    , data(_int::move(cd.data))
    , is_valid(cd.is_valid) {
    cd.is_valid = false;
  }

  CDeferred &operator=(CDeferred && cd) {
    if (this != &cd) {
      this->~CDeferred();
      new (this) CDeferred(_int::move(cd));
    }
    return *this;
  }

  void
  cancel() {
    is_valid = false;
  }

  ~CDeferred() { if (is_valid) f(data); }
};

template <class F, class T, class V>
class CDeferred<F(T,V)> {
  F f;
  T a1;
  V a2;
  bool is_valid;

public:
  CDeferred()
    : is_valid(false) {}

  CDeferred(F f_, T t, V v)
    : f(f_)
    , a1(_int::move(t))
    , a2(_int::move(v))
    , is_valid(true) {}

  CDeferred(CDeferred && cd)
    : f(_int::move(cd.f))
    , a1(_int::move(cd.a1))
    , a2(_int::move(cd.a2))
    , is_valid(cd.is_valid) {
    cd.is_valid = false;
  }

  CDeferred &operator=(CDeferred && cd) {
    if (this != &cd) {
      this->~CDeferred();
      new (this) CDeferred(_int::move(cd));
    }
    return *this;
  }

  ~CDeferred() { if (is_valid) f(a1, a2); }

  void
  cancel() {
    is_valid = false;
  }
};

template <class Destroyer, class... Args>
CDeferred<Destroyer(Args...)> create_deferred(Destroyer f, Args ...args) {
  return CDeferred<Destroyer(Args...)>(_int::move(f), _int::move(args)...);
}

}

#endif

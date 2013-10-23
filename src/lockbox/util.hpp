#ifndef _lockbox_util_H
#define _lockbox_util_H

#include <cstring>

namespace lockbox {

/* this little class allows us to get C++ RAII with C based data structures */
template <class T, class Destroyer>
class CDestroyer {
private:
  const T & myt;
  Destroyer f;
public:
  CDestroyer(const T & var, Destroyer f_) : myt(var), f(f_) {}
  CDestroyer(const CDestroyer & var) = delete;
  CDestroyer(CDestroyer && var) = default;
  ~CDestroyer() { f(myt);};
};

template <class T, class Destroyer>
CDestroyer<T, Destroyer> create_destroyer(const T & obj, Destroyer f) {
  return CDestroyer<T, Destroyer>(obj, f);
}

template <class T>
void
zero_object(T & obj) {
  memset(&obj, 0, sizeof(obj));
}

}

#endif

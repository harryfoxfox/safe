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

#ifndef __either_hpp
#define __either_hpp

#include <new>
#include <stdexcept>
#include <type_traits>

namespace eit {

class bad_either_access : public std::logic_error {
public:
  using std::logic_error::logic_error;
};

template <class LeftType,
          class RightType,
          typename std::enable_if<!std::is_reference<LeftType>::value, int>::type = 0,
          typename std::enable_if<!std::is_reference<RightType>::value, int>::type = 0>
class either {
 private:
  union {
    LeftType _left;
    RightType _right;
  };
  bool _is_left;

  void _deconstruct() {
    if (_is_left) {
      _left.~LeftType();
    }
    else {
      _right.~RightType();
    }
  }

 public:
  constexpr
  either(LeftType left)
    : _left(std::move(left)), _is_left(true) {}

  constexpr
  either(RightType right)
    : _right(std::move(right)), _is_left(false) {}

  template<typename Either>
  either(Either && val) {
    _is_left = val.has_left();
    if (val.has_left()) {
      new (&_left) LeftType(std::forward<Either>(val).left());
    }
    else {
      new (&_right) RightType(std::forward<Either>(val).right());
    }
  }

  ~either() {
    _deconstruct();
  }

  template<typename Either>
  either &
  operator=(Either && val) {
    _deconstruct();
    new (this) either(std::forward<Either>(val));
    return *this;
  }

  constexpr
  bool
  has_left() const {
    return _is_left;
  }

  const LeftType &
  left() const & {
    if (!has_left()) throw bad_either_access("bad either access");
    return _left;
  }

  LeftType &
  left() & {
    if (!has_left()) throw bad_either_access("bad either access");
    return _left;
  }

  LeftType &&
  left() && {
    if (!has_left()) throw bad_either_access("bad either access");
    return std::move(_left);
  }

  const RightType &
  right() const & {
    if (has_left()) throw bad_either_access("bad either access");
    return _right;
  }

  RightType &
  right() & {
    if (has_left()) throw bad_either_access("bad either access");
    return _right;
  }

  RightType &&
  right() && {
    if (has_left()) throw bad_either_access("bad either access");
    return std::move(_right);
  }
};

}

#endif

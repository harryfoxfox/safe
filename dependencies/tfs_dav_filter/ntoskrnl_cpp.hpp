/*
  Lockbox: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef __tfs_dav_store_ntoskernel_cxx_hpp
#define __tfs_dav_store_ntoskernel_cxx_hpp

#include <ntifs.h>

#ifdef __GNUC__

/* These are default version of placement new, they
   should be available globally by the compiler but this is
   not the case with g++ */
inline void *operator new (size_t, void *p) throw() { return p ; }
inline void *operator new[] (size_t, void *p) throw() { return p ; }
inline void operator delete (void *, void *) throw() { }
inline void operator delete[] (void *, void *) throw() { }

#endif

namespace std {

#undef min

template <class T>
const T &
min(const T & a, const T & b) {
  return a > b ? b : a;
}

}

typedef UCHAR uint8_t;
typedef USHORT uint16_t;
typedef ULONG uint32_t;

#define assert ASSERT

#endif

/*
  Safe: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

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

#ifndef _unicode_fs_hpp
#define _unicode_fs_hpp

// yes i know defines are suboptimal, but it's mostly contained
#ifdef __APPLE__
#include <safe/mac/unicode_fs.hpp>
#define __NS mac;
#elif _WIN32
#include <safe/win/unicode_fs.hpp>
#define __NS win
#else
#error unicode_fs not supported on this platform
#endif

namespace safe { namespace unicode_fs {

using namespace safe::unicode_fs::__NS;

}}

#undef __NS

#endif

/*
  Lockbox: Encrypted File System
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

#ifdef __APPLE__
#include <lockbox/unicode_fs_mac.hpp>
#endif

namespace lockbox { namespace unicode_fs {


// yes i know defines are suboptimal, but it's mostly contained
#ifdef __APPLE__
 using namespace lockbox::unicode_fs::mac;
#endif

}}

#endif
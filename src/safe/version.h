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

#ifndef __safe_version_h
#define __safe_version_h

// the major version number indicates new release
// the build version number indicates bug fixes

#define SAFE_VERSION_MAJOR 0
#define SAFE_VERSION_BUILD 6

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifdef NDEBUG
#define SAFE_VERSION_STR \
  STR(SAFE_VERSION_MAJOR) "." STR(SAFE_VERSION_BUILD)
#else
#define SAFE_VERSION_STR \
  STR(SAFE_VERSION_MAJOR) "." STR(SAFE_VERSION_BUILD) "-debug"
#endif

#endif

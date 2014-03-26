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

#ifndef __w32util_sync_hpp
#define __w32util_sync_hpp

#include <safe/deferred.hpp>

#include <windows.h>

namespace w32util {

class CriticalSection {
private:
  CRITICAL_SECTION _cs;

public:
  CriticalSection();
  ~CriticalSection();

  // delete copy methods
  CriticalSection(const CriticalSection &) = delete;
  CriticalSection &operator=(const CriticalSection &) = delete;

  void
  enter();

  void
  leave();

  decltype(safe::create_deferred(LeaveCriticalSection, (CRITICAL_SECTION *) nullptr))
  create_guard();
};

class Event {
  HANDLE _event;

public:
  Event(bool manual_reset);
  ~Event();

  // delete copy constructors
  Event(const CriticalSection &) = delete;
  Event &operator=(const Event &) = delete;

  void
  set();

  HANDLE
  get_handle();
};

}


#endif

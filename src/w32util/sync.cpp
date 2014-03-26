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

#include <w32util/sync.hpp>

#include <w32util/error.hpp>

#include <windows.h>

namespace w32util {

CriticalSection::CriticalSection() {
  InitializeCriticalSection(&_cs);
}

CriticalSection::~CriticalSection() {
  DeleteCriticalSection(&_cs);
}

void
CriticalSection::enter() {
  EnterCriticalSection(&_cs);
}

void
CriticalSection::leave() {
  LeaveCriticalSection(&_cs);
}

decltype(safe::create_deferred(LeaveCriticalSection, (CRITICAL_SECTION *) nullptr))
CriticalSection::create_guard() {
  enter();
  return safe::create_deferred(LeaveCriticalSection, &_cs);
}

Event::Event(bool manual_reset) {
  _event = check_null(CreateEvent, nullptr, manual_reset, FALSE, nullptr);
}

Event::~Event() {
  CloseHandle(_event);
}

void
Event::set() {
  check_bool(SetEvent, _event);
}

HANDLE
Event::get_handle() {
  return _event;
}

}

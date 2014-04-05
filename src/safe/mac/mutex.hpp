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

#ifndef __safe_mac_mutex_hpp
#define __safe_mac_mutex_hpp

#include <safe/deferred.hpp>

#include <system_error>

#include <pthread.h>

namespace safe { namespace mac {

class Mutex {
  pthread_mutex_t _mutex;

public:
  Mutex() {
    auto ret = pthread_mutex_init(&_mutex, nullptr);
    if (ret) throw std::system_error(ret, std::generic_category());
  }

  ~Mutex() {
    pthread_mutex_destroy(&_mutex);
  }

  decltype(safe::create_deferred(pthread_mutex_unlock, &_mutex))
  create_guard() {
    auto ret = pthread_mutex_lock(&_mutex);
    if (ret) throw std::system_error(ret, std::generic_category());
    return safe::create_deferred(pthread_mutex_unlock, &_mutex);
  }
};

}}

#endif

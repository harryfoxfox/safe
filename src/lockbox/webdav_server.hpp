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

#ifndef _lockbox_webdav_server_hpp
#define _lockbox_webdav_server_hpp

#include <encfs/fs/FsIO.h>

#include <davfuse/event_loop.h>
#include <davfuse/util_sockets.h>

#include <functional>
#include <memory>
#include <string>

#ifndef _CXX_STATIC_BUILD
#define CXX_STATIC_ATTR
#endif

namespace lockbox {

CXX_STATIC_ATTR
bool
global_webdav_init();

CXX_STATIC_ATTR
void
global_webdav_shutdown();

CXX_STATIC_ATTR
void
run_webdav_server(std::shared_ptr<encfs::FsIO> fs_io,
                  encfs::Path root_path,
                  ipv4_t ipaddr,
                  port_t port,
                  const std::string & mount_name,
                  std::function<void(event_loop_handle_t)> when_done);

}

#ifdef _CXX_STATIC_BUILD
#include <lockbox/webdav_server.cpp>
#else
#undef CXX_STATIC_ATTR
#endif

#endif

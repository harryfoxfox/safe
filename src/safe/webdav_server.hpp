/*
  Safe: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

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

#ifndef _safe_webdav_server_hpp
#define _safe_webdav_server_hpp

#include <safe/util.hpp>

#include <encfs/fs/FsIO.h>

#include <davfuse/sockets.h>
#include <davfuse/util_sockets.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#ifndef _CXX_STATIC_BUILD
#define CXX_STATIC_ATTR
#endif

namespace safe {

struct SocketDestroyer {
  void operator()(socket_t s) {
    auto ret = closesocket(s);
    if (ret == SOCKET_ERROR) throw std::runtime_error("failure to close socket");
  }
};

typedef ManagedResource<socket_t, SocketDestroyer> ManagedSocket;

/* NB: this handle is thread safe */
class WebdavServerHandle {
  ManagedSocket _send_socket;

public:
  WebdavServerHandle(ManagedSocket send_socket)
    : _send_socket(std::move(send_socket)) {}

  ~WebdavServerHandle();

  WebdavServerHandle(WebdavServerHandle &&) = default;
  WebdavServerHandle &operator=(WebdavServerHandle &&) = default;

  void signal_stop() const;
  void signal_disconnect_all_clients() const;
};

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
                  std::function<void(WebdavServerHandle)> when_done);

std::string
get_webdav_url_root(port_t port);

std::string
get_webdav_mount_url(port_t port, std::string mount_name);

}

#ifdef _CXX_STATIC_BUILD
#include <safe/webdav_server.cpp>
#else
#undef CXX_STATIC_ATTR
#endif

#endif

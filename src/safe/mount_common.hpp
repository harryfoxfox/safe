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

#ifndef __safe_mount_common_hpp
#define __safe_mount_common_hpp

#include <safe/fs.hpp>
#include <safe/webdav_server.hpp>

#include <encfs/fs/FileUtils.h>
#include <encfs/fs/FsIO.h>

#include <memory>
#include <string>

#include <cstdlib>
#include <ctime>

namespace safe {

template <typename MountEvent>
struct ServerThreadParams {
  std::shared_ptr<MountEvent> mount_event_p;
  // the control-block of std::shared_ptr is thread-safe
  // (i.e. multiple threads of control can have a copy of a root shared_ptr)
  // a single std::shared_ptr is not thread-safe, this use-case doesn't apply to us
  // for proper operation the encfs::FsIO implementation used
  // must be thread-safe as well, the mac native fs is thread-safe so that isn't a concern
  std::shared_ptr<encfs::FsIO> native_fs;
  encfs::Path encrypted_directory_path;
  encfs::EncfsConfig encfs_config;
  encfs::SecureMem password;
  std::string mount_name;
};

template <typename MountEvent>
void
mount_thread_fn(std::unique_ptr<ServerThreadParams<MountEvent>> params) {
  std::srand(std::time(nullptr));

  auto enc_fs =
    safe::create_enc_fs(std::move(params->native_fs),
                           params->encrypted_directory_path,
                           std::move(params->encfs_config),
                           std::move(params->password));

  // we only listen on localhost
  auto ip_addr = LOCALHOST_IP;
  auto listen_port =
    find_random_free_listen_port(ip_addr, PRIVATE_PORT_START, PRIVATE_PORT_END);

  bool sent_signal = false;

  auto our_callback = [&] (WebdavServerHandle webhandle) {
    params->mount_event_p->set_mount_success(listen_port, std::move(webhandle));
    sent_signal = true;
  };

  safe::run_webdav_server(std::move(enc_fs),
                             std::move(params->encrypted_directory_path),
                             ip_addr,
                             listen_port,
                             std::move(params->mount_name),
                             our_callback);

  if (!sent_signal) params->mount_event_p->set_mount_fail();

  params->mount_event_p->set_thread_done();

  // server is done, possible unmount
}

}

#endif

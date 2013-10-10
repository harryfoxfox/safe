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

#include <encfs/cipher/MemoryPool.h>

#include <davfuse/c_util.h>
#include <davfuse/http_backend_sockets_fdevent.h>
#include <davfuse/http_backend_sockets_fdevent_fdevent.h>
#include <davfuse/iface_util.h>
#include <davfuse/logging.h>
#include <davfuse/logging_log_printer.h>
#include <davfuse/webdav_backend_fs.h>
#include <davfuse/webdav_backend_fs_fs.h>
#include <davfuse/webdav_server.h>
#include <davfuse/webdav_server_xml.h>
#include <davfuse/uthread.h>
#include <davfuse/util.h>
#include <davfuse/util_sockets.h>

#include <lockbox/util.hpp>

#include <lockbox/lockbox_serer.hpp>

ASSERT_SAME_IMPL(HTTP_SERVER_HTTP_BACKEND_IMPL,
                 HTTP_BACKEND_SOCKETS_FDEVENT_IMPL);
ASSERT_SAME_IMPL(WEBDAV_SERVER_WEBDAV_BACKEND_IMPL,
                 WEBDAV_BACKEND_FS_IMPL);

namespace lockbox {

bool
global_webdav_init() {
  /* init sockets */
  bool success_init_sockets = false;
  bool success_init_xml = false;

  success_init_sockets = init_socket_subsystem();
  if (!success_init_sockets) {
    goto fail;
  }

  /* init xml parser */
  init_xml_parser();
  success_init_xml = true;

  return true;

 fail:
  if (success_init_xml) {
    shutdown_xml_parser();
  }

  if (success_init_sockets) {
    shutdown_socket_subsystem();
  }

  return false;
}

void
global_webdav_shutdown() {
  shutdown_xml_parser();

  log_info("Shutting down socket subsystem");
  shutdown_socket_subsystem();
}

std::shared_ptr<FsIO>
create_base_fs() {
  const auto base_fs = fs_default_new();
  if (!base_fs) throw std::runtime_error("error while creating base fs!");
  const bool destroy_fs_on_delete = true;
  return std::make_shared<CFsToFsIO>(base_fs, destroy_fs_on_delete);
}

encfs::EncfsConfig
read_encfs_config(std::shared_ptr<FsIO> fs_io,
                  const encfs::Path & encrypted_folder_path) {
  // TODO: implemenent fails with a catchable exception i.e.
  // EncryptedFolderDoesNotExist
  // NoConfigurationFile
  // BadlyFormattedFile
  return EncfsFsOpts();
}

void
write_encfs_config(std::shared_ptr<FsIO> fs_io,
                   const encfs::Path & encrypted_folder_path,b
                   const encfs::EncfsConfig &cfg) {
}

bool verify_password(const encfs::EncfsConfig & cfg, const encfs::SecureMem *password) {
  return false;
}

std::shared_ptr<FsIO>
create_enc_fs(std::shared_ptr<FsIO> base_fs_io,
              encfs::Path encrypted_folder_path,
              const encfs::EncfsConfig & cfg,
              const encfs::SecureMem *password) {
  // encfs options
  auto encfs_opts = std::make_shared<EncFS_Opts>();
  encfs_opts->fs_io = std::move( base_fs_io );
  encfs_opts->rootDir = std::move( encrypted_folder_path );
  // TODO: add password reader

  // encfs
  auto encfs_io = std::make_shared<encfs::EncfsFsIO>();
  encfs_io->initFS(std::move(encfs_opts), cfg);

  return encfs_io;
}

struct RunningCallbackCtx {
  int send_sock;
  int recv_sock;
  fdevent_t loop;
  std::function<void(fdevent_t)> fn;
};

static
EVENT_HANDLER_DEFINE(_when_server_runs, ev_type, ev, ud) {
  RunningCallbackCtx *ctx = ud;
  close(ctx->send_sock);
  close(ctx->recv_sock);
  ctx->fn(ctx->loop);
}

void
run_lockbox_webdav_server(shared_ptr<FsIO> fs_io,
                          char *root_path,
                          port_t port,
                          std::function<void(fdevent_t))> when_done) {
  // create event loop (implemented by file descriptors)
  fdevent_loop_t loop = fdevent_default_new();
  if (!loop) throw std::runtime_error("Couldn't create event loop");
  auto _free_loop = create_destroyer(loop, fdevent_destroy);

  struct sockaddr_in listen_addr;
  init_sockaddr_in(&listen_addr, port);

  // create network IO backend (implemented by the Socket API)
  auto network_io =
    http_backend_sockets_fdevent_new(loop,
                                     (struct sockaddr *) &listen_addr,
                                     sizeof(listen_addr));
  if (!http_backend) throw std::runtime_error("Couldn't create network io");
  auto _destroy_network_io =
    create_destroyer(fs, http_backend_sockets_fdevent_destroy);

  // create server storage backend (implemented by the file system)
  auto server_backend = webdav_backend_fs_new(fs_io.get(), root_path);
  if (!server_backend) {
    throw std::runtime_error("Couldn't create webdav server backend");
  }
  auto _destroy_server_backend = create_destroyer(server_backend,
                                                  webdav_backend_fs_destroy);

  // create server
  auto public_uri_root = "http://localhost:" + std::to_string(port) + "/";
  auto internal_root = "/";
  auto server = webdav_server_start(network_io,
                                    public_uri_root.c_str(),
                                    internal_root,
                                    server_backend);
  if (!server) throw std::runtime_error("Couldn't start webdav server");

  // now set up callback
  int sv[2];
  auto ret_socketpair = localhost_socketpair(sv);
  if (ret_socketpair) abort();
  auto ret_send = send(sv[0], "1", 1);
  if (ret_send) throw std::runtime_error("couldn't set up startup callback");
  auto ctx = RunningCallbackCtx(sv[0], sv[1], loop, std::move(when_done));
  auto success_add_watch =
    fdevent_add_watch(loop, sv[1], create_stream_events(true, false),
                      _when_server_runs, &ctx, NULL);
  if (!success_add_watch) {
    throw std::runtime_error("couldn't set up startup callback");
  }

  // run server
  log_info("Starting main loop");
  fdevent_main_loop(loop);
  log_info("Server stopped");
}

}

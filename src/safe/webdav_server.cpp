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

#include <safe/webdav_server.hpp>

#include <safe/fs_fsio.h>
#include <safe/util.hpp>

#include <encfs/fs/FsIO.h>

#include <davfuse/c_util.h>
#include <davfuse/event_loop.h>
#include <davfuse/http_helpers.h>
#include <davfuse/iface_util.h>
#include <davfuse/logging.h>
#include <davfuse/fs_dynamic.h>
#include <davfuse/sockets.h>
#include <davfuse/webdav_backend_fs.h>
#include <davfuse/webdav_server.h>
#include <davfuse/webdav_server_xml.h>
#include <davfuse/util.h>
#include <davfuse/util_sockets.h>

#include <sstream>

ASSERT_SAME_IMPL(FS_IMPL, FS_DYNAMIC_IMPL);
ASSERT_SAME_IMPL(WEBDAV_BACKEND_IMPL, WEBDAV_BACKEND_FS_IMPL);

#ifndef _CXX_STATIC_BUILD
#define CXX_STATIC_ATTR
#endif

namespace safe {

CXX_STATIC_ATTR
bool
global_webdav_init() {
  /* init sockets */
  bool success_init_sockets = false;
  bool success_init_xml = false;
  bool success_ignore_sigpipe = false;

  success_init_sockets = init_socket_subsystem();
  if (!success_init_sockets) {
    goto fail;
  }

  /* init xml parser */
  init_xml_parser();
  success_init_xml = true;

  success_ignore_sigpipe = ignore_sigpipe();
  if (!success_ignore_sigpipe) goto fail;

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

CXX_STATIC_ATTR
void
global_webdav_shutdown() {
  shutdown_xml_parser();

  log_info("Shutting down socket subsystem");
  shutdown_socket_subsystem();
}

static const FsOperations fsio_ops = {
  .open = (fs_dynamic_open_fn) fs_fsio_open,
  .fgetattr = (fs_dynamic_fgetattr_fn) fs_fsio_fgetattr,
  .ftruncate = (fs_dynamic_ftruncate_fn) fs_fsio_ftruncate,
  .read = (fs_dynamic_read_fn) fs_fsio_read,
  .write = (fs_dynamic_write_fn) fs_fsio_write,
  .close = (fs_dynamic_close_fn) fs_fsio_close,
  .opendir = (fs_dynamic_opendir_fn) fs_fsio_opendir,
  .readdir = (fs_dynamic_readdir_fn) fs_fsio_readdir,
  .closedir = (fs_dynamic_closedir_fn) fs_fsio_closedir,
  .remove = (fs_dynamic_remove_fn) fs_fsio_remove,
  .mkdir = (fs_dynamic_mkdir_fn) fs_fsio_mkdir,
  .getattr = (fs_dynamic_getattr_fn) fs_fsio_getattr,
  .rename = (fs_dynamic_rename_fn) fs_fsio_rename,
  .set_times = (fs_dynamic_set_times_fn) fs_fsio_set_times,
  .path_is_root = (fs_dynamic_path_is_root_fn) fs_fsio_path_is_root,
  .path_is_valid = (fs_dynamic_path_is_valid_fn) fs_fsio_path_is_valid,
  .path_dirname = (fs_dynamic_path_dirname_fn) fs_fsio_path_dirname,
  .path_basename = (fs_dynamic_path_basename_fn) fs_fsio_path_basename,
  .path_join = (fs_dynamic_path_join_fn) fs_fsio_path_join,
  .destroy = (fs_dynamic_destroy_fn) fs_fsio_destroy,
};

struct RunningCallbackCtx {
  webdav_server_t ws;
  ManagedSocket send_socket;
  ManagedSocket recv_socket;
  event_loop_handle_t loop;
  std::function<void(WebdavServerHandle)> fn;
};

CXX_STATIC_ATTR
static
EVENT_HANDLER_DEFINE(_webdav_serv_handle_action, ev_type, ev, ud) {
  (void) ev_type;
  (void) ev;
  auto ctx = (RunningCallbackCtx *) ud;

  auto received_stop = false;
  while (!received_stop) {
    char buf[32];
    auto amt_read = recv(ctx->recv_socket.get(), buf, sizeof(buf), 0);
    if (amt_read == SOCKET_ERROR) {
      if (last_socket_error() == SOCKET_EAGAIN ||
          last_socket_error() == SOCKET_EWOULDBLOCK) break;
      /* TODO: handle more gracefully */
      else throw std::runtime_error("failed to call recv");
    }

    assert(amt_read >= 0);
    for (size_t i = 0; i < (size_t) amt_read; ++i) {
      switch (buf[i]) {
      case '0': {
        webdav_server_stop(ctx->ws);
        received_stop = true;
        break;
      }
      case '1': {
        webdav_server_disconnect_existing_clients(ctx->ws);
        break;
      }
      default: {
        /* should never happen */
        assert(false);
        lbx_log_error("Random code received: %c", buf[i]);
      }
      }
    }
  }

  if (received_stop) return;

  auto success = event_loop_socket_watch_add(ctx->loop, ctx->recv_socket.get(),
                                             create_stream_events(true, false),
                                             _webdav_serv_handle_action,
                                             ud,
                                             NULL);
  // TODO: handle error more gracefully
  if (!success) throw std::runtime_error("error adding socket watch");
}

CXX_STATIC_ATTR
static
EVENT_HANDLER_DEFINE(_when_server_runs, ev_type, ev, ud) {
  (void) ev_type;
  (void) ev;
  auto ctx = (RunningCallbackCtx *) ud;

  /* set up callback */
  auto success = event_loop_socket_watch_add(ctx->loop, ctx->recv_socket.get(),
                                             create_stream_events(true, false),
                                             _webdav_serv_handle_action,
                                             ud, NULL);
  // TODO: handle error more gracefully
  if (!success) throw std::runtime_error("error adding socket watch");

  ctx->fn(WebdavServerHandle(ctx->send_socket));
}

WebdavServerHandle::~WebdavServerHandle() {
  // if we haven't been moved and we are being destroyed
  // then signal to our thread to stop
  // this allows for automatic thread cleanup
  if (_send_socket) signal_stop();
}

void WebdavServerHandle::signal_stop() const {
  auto ret_send = send(_send_socket.get(), "0", 1, 0);
  if (ret_send == -1) throw std::runtime_error("error calling send()");
}

void WebdavServerHandle::signal_disconnect_all_clients() const {
  auto ret_send = send(_send_socket.get(), "1", 1, 0);
  if (ret_send == -1) throw std::runtime_error("error calling send");
}

CXX_STATIC_ATTR
void
run_webdav_server(std::shared_ptr<encfs::FsIO> fs_io,
                  encfs::Path root_path,
                  ipv4_t ipaddr,
                  port_t port,
                  const std::string & mount_name,
                  std::function<void(WebdavServerHandle)> when_done) {
  // create event loop (implemented by file descriptors)
  auto loop = event_loop_default_new();
  if (!loop) throw std::runtime_error("Couldn't create event loop");
  auto _free_loop = create_destroyer(loop, event_loop_destroy);

  // create listen socket
  struct sockaddr_in listen_addr;
  init_sockaddr_in(&listen_addr, ipaddr, port);
  const socket_t sock = create_bound_socket((struct sockaddr *) &listen_addr,
                                            sizeof(listen_addr));
  if (sock == INVALID_SOCKET) {
    throw std::runtime_error("Couldn't create listen socket!");
  }
  auto _destroy_listen_socket =
    create_destroyer(sock, closesocket);

  // create cfs for webdav backend
  const bool destroy_fs_on_delete = false;
  const auto cfs = fs_dynamic_new((void *) fs_io.get(), &fsio_ops,
                                  destroy_fs_on_delete);
  if (!cfs) throw std::runtime_error("Couldn't create backend fs");
  auto _destroy_cfs =
    create_destroyer(cfs, fs_dynamic_destroy);

  // create server storage backend (implemented by the file system)
  auto server_backend = webdav_backend_fs_new(cfs, root_path.c_str());
  if (!server_backend) {
    throw std::runtime_error("Couldn't create webdav server backend");
  }
  auto _destroy_server_backend = create_destroyer(server_backend,
                                                  webdav_backend_fs_destroy);

  // create server
  std::ostringstream build_uri_root;
  // NB: use "127.0.0.1" here instead of "localhost"
  //     windows prefers ipv6 by default and we aren't
  //     listening on ipv6, so that will slow down connections
  build_uri_root << "http://127.0.0.1:" << port << "/";
  auto public_uri_root = std::move(build_uri_root).str();
  auto internal_root = "/" + mount_name;
  const auto encoded_internal_root =
    encode_urlpath(internal_root.data(), internal_root.size());
  if (!encoded_internal_root) {
    throw std::runtime_error("Couldn't encode path");
  }
  auto _destroy_encoded_internal_root =
    create_destroyer(encoded_internal_root, free);
  auto server = webdav_server_new(loop, sock, public_uri_root.c_str(),
                                  encoded_internal_root, server_backend);
  if (!server) throw std::runtime_error("Couldn't create webdav server");
  auto _destroy_server = create_destroyer(server, webdav_server_destroy);

  // start server
  auto success_server_start = webdav_server_start(server);
  if (!success_server_start) throw std::runtime_error("Couldn't create webdav server");

  // now set up callback
  socket_t sv[2];
  auto ret = localhost_socketpair(sv);
  if (ret < 0) throw std::runtime_error("error calling localhost_socketpair");

  auto success = set_socket_non_blocking(sv[0]);
  if (!success) throw std::runtime_error("error calling set_socket_non_blocking");

  auto success_2 = set_socket_non_blocking(sv[1]);
  if (!success_2) throw std::runtime_error("error calling set_socket_non_blocking");

  auto ctx = RunningCallbackCtx {server,
                                 ManagedSocket(sv[0]), ManagedSocket(sv[1]),
                                 loop, std::move(when_done)};
  auto timeout = EventLoopTimeout {0, 0};
  auto success_add_watch =
    event_loop_timeout_add(loop, &timeout, _when_server_runs, &ctx, NULL);
  if (!success_add_watch) {
    throw std::runtime_error("couldn't set up startup callback");
  }

  // run server
  log_info("Starting main loop");
  auto success_loop = event_loop_main_loop(loop);
  if (!success_loop) throw std::runtime_error("Loop failed");

  log_info("Server stopped");
}

}

#ifndef _CXX_STATIC_BUILD
#undef CXX_STATIC_ATTR
#endif

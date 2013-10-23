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
#include <encfs/fs/EncfsFsIO.h>
#include <encfs/fs/FileUtils.h>

#include <davfuse/c_util.h>
#include <davfuse/http_backend_sockets_fdevent.h>
#include <davfuse/fdevent.h>
#include <davfuse/iface_util.h>
#include <davfuse/logging.h>
#include <davfuse/webdav_backend_fs.h>
#include <davfuse/fs.h>
#include <davfuse/fs_dynamic.h>
#include <davfuse/fs_native.h>
#include <davfuse/sockets.h>
#include <davfuse/webdav_server.h>
#include <davfuse/webdav_server_xml.h>
#include <davfuse/uthread.h>
#include <davfuse/util.h>
#include <davfuse/util_sockets.h>

#include <lockbox/CFsToFsIO.hpp>
#include <lockbox/SecureMemPasswordReader.hpp>
#include <lockbox/fs_fsio.h>
#include <lockbox/util.hpp>

#include <lockbox/lockbox_server.hpp>

#include <sstream>

ASSERT_SAME_IMPL(FS_IMPL, FS_DYNAMIC_IMPL);
ASSERT_SAME_IMPL(HTTP_BACKEND_IMPL, HTTP_BACKEND_SOCKETS_FDEVENT_IMPL);
ASSERT_SAME_IMPL(WEBDAV_BACKEND_IMPL, WEBDAV_BACKEND_FS_IMPL);

#ifndef _CXX_STATIC_BUILD
#define CXX_STATIC_ATTR
#endif

namespace lockbox {

CXX_STATIC_ATTR
int
localhost_socketpair(fd_t sv[2]) {
  fd_t socket_fd = INVALID_SOCKET;
  fd_t client_fd = INVALID_SOCKET;
  int ret_connect = 0;
  int ret_accept = 0;
  const unsigned ATTEMPT_LIMIT = 10;
  port_t target_port = 19872;
  bool success = false;
  int filled;

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == INVALID_SOCKET) goto fail;

  for (unsigned attempts = 0; !success && attempts < ATTEMPT_LIMIT;
       ++attempts) {
    ++target_port;
    struct sockaddr_in listen_addr;
    init_sockaddr_in(&listen_addr, 0x7f000001, target_port);

    auto ret_bind = bind(socket_fd,
                         (struct sockaddr *) &listen_addr,
                         sizeof(listen_addr));
    if (ret_bind) continue;

    auto ret_listen = listen(socket_fd, 5);
    if (ret_listen) continue;

    success = true;
  }

  if (!success) goto fail;

  client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd == INVALID_SOCKET) goto fail;

  struct sockaddr_in connect_addr;
  init_sockaddr_in(&connect_addr, 0x7f000001, target_port);

  ret_connect =
    connect(client_fd, (struct sockaddr *) &connect_addr,
            sizeof(connect_addr));
  if (ret_connect) goto fail;

  filled = sizeof(connect_addr);
  ret_accept =
    accept(socket_fd,
           (struct sockaddr *) &connect_addr,
           &filled);
  if (ret_accept == INVALID_SOCKET) goto fail;

  sv[0] = ret_accept;
  sv[1] = client_fd;

  closesocket(socket_fd);

  return 0;

 fail:
  // TODO: log if close() fails
  if (socket_fd != INVALID_SOCKET) closesocket(socket_fd);

  // TODO: log if close() fails
  if (client_fd != INVALID_SOCKET) closesocket(client_fd);

  return -1;
}

template<class T>
T gcd(T a, T b) {
  while (b) {
    auto t = b;
    b = a % b;
    a = t;
  }
  return a;
}

template<class T>
bool are_mutually_prime(T a, T b) {
  return b && a && gcd(a, b) == 1;
}

template<class T>
T find_random_smaller_mutually_prime(T a) {
  // monte carlo
  T toret;
  do {
    toret = (std::rand() % (a - 1)) + 1;
  }
  while (are_mutually_prime(a, toret));

  // 0 is not mutually prime with anything
  assert(toret);

  return toret;
}

CXX_STATIC_ATTR
port_t
find_random_free_listen_port(ipv4_t ip, port_t low, port_t high) {
  // find a port in the range (inclusive) to bind to
  // based on searching a random permutation of the numbers
  // in the range

  if (low > high) {
    throw std::invalid_argument("low bound must be <= high");
  }

  auto socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == INVALID_SOCKET) {
    throw std::runtime_error("Couldnt create socket");
  }
  auto _close_socket = lockbox::create_destroyer(socket_fd, closesocket);

  auto range_size = high - low + 1;

  auto cur_port = low + std::rand() % range_size;
  auto adder = high == low
    ? 0 // we're just iterating once anyway
    : find_random_smaller_mutually_prime(range_size);

  // TODO: would be nice to implement and use a C++ iterator
  //       instead of this
  for (port_t i = 0; i < range_size; ++i) {
    struct sockaddr_in listen_addr;
    init_sockaddr_in(&listen_addr, ip, cur_port);

    auto ret_bind = bind(socket_fd,
                         (struct sockaddr *) &listen_addr,
                         sizeof(listen_addr));
    if (!ret_bind) return cur_port;
    if (last_socket_error() != SOCKET_EADDRINUSE) {
      throw std::runtime_error("Bind ERROR!");
    }

    cur_port += adder;

    // deal with wraparound
    // NB: this is unsigned overflow so it's not undefined
    if (cur_port > high) cur_port = low + (cur_port - high - 1);
    else if (cur_port < low) cur_port += low;
  }

  throw std::runtime_error("no free ports!");
}


CXX_STATIC_ATTR
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

CXX_STATIC_ATTR
void
global_webdav_shutdown() {
  shutdown_xml_parser();

  log_info("Shutting down socket subsystem");
  shutdown_socket_subsystem();
}

static const FsOperations native_ops = {
  .open = (fs_dynamic_open_fn) fs_native_open,
  .fgetattr = (fs_dynamic_fgetattr_fn) fs_native_fgetattr,
  .ftruncate = (fs_dynamic_ftruncate_fn) fs_native_ftruncate,
  .read = (fs_dynamic_read_fn) fs_native_read,
  .write = (fs_dynamic_write_fn) fs_native_write,
  .close = (fs_dynamic_close_fn) fs_native_close,
  .opendir = (fs_dynamic_opendir_fn) fs_native_opendir,
  .readdir = (fs_dynamic_readdir_fn) fs_native_readdir,
  .closedir = (fs_dynamic_closedir_fn) fs_native_closedir,
  .remove = (fs_dynamic_remove_fn) fs_native_remove,
  .mkdir = (fs_dynamic_mkdir_fn) fs_native_mkdir,
  .getattr = (fs_dynamic_getattr_fn) fs_native_getattr,
  .rename = (fs_dynamic_rename_fn) fs_native_rename,
  .set_times = (fs_dynamic_set_times_fn) fs_native_set_times,
  .path_is_root = (fs_dynamic_path_is_root_fn) fs_native_path_is_root,
  .path_sep = (fs_dynamic_path_sep_fn) fs_native_path_sep,
  .path_equals = (fs_dynamic_path_equals_fn) fs_native_path_equals,
  .path_is_parent = (fs_dynamic_path_is_parent_fn) fs_native_path_is_parent,
  .path_is_valid = (fs_dynamic_path_is_valid_fn) fs_native_path_is_valid,
  .destroy = (fs_dynamic_destroy_fn) fs_native_destroy,
};

CXX_STATIC_ATTR
std::shared_ptr<encfs::FsIO>
create_native_fs() {
  const bool destroy_fs_on_delete = true;
  const auto native_fs = fs_native_default_new();
  if (!native_fs) throw std::runtime_error("error while creating posix fs!");
  const auto base_fs = fs_dynamic_new(native_fs, &native_ops, destroy_fs_on_delete);
  if (!base_fs) {
    fs_native_destroy(native_fs);
    throw std::runtime_error("error while creating base fs!");
  }
  return std::make_shared<CFsToFsIO>(base_fs, destroy_fs_on_delete);
}

CXX_STATIC_ATTR
std::shared_ptr<encfs::FsIO>
create_enc_fs(std::shared_ptr<encfs::FsIO> base_fs_io,
              encfs::Path encrypted_folder_path,
              const encfs::EncfsConfig & cfg,
              encfs::SecureMem password) {
  // encfs options
  auto encfs_opts = std::make_shared<encfs::EncFS_Opts>();
  encfs_opts->fs_io = std::move(base_fs_io);
  encfs_opts->rootDir = std::move(encrypted_folder_path);
  encfs_opts->passwordReader = std::make_shared<SecureMemPasswordReader>(std::move(password));

  // encfs
  auto encfs_io = std::make_shared<encfs::EncfsFsIO>();
  encfs_io->initFS(std::move(encfs_opts), cfg);

  return encfs_io;
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
  .path_sep = (fs_dynamic_path_sep_fn) fs_fsio_path_sep,
  .path_equals = (fs_dynamic_path_equals_fn) fs_fsio_path_equals,
  .path_is_parent = (fs_dynamic_path_is_parent_fn) fs_fsio_path_is_parent,
  .path_is_valid = (fs_dynamic_path_is_valid_fn) fs_fsio_path_is_valid,
  .destroy = (fs_dynamic_destroy_fn) fs_fsio_destroy,
};

struct RunningCallbackCtx {
  fd_t send_sock;
  fd_t recv_sock;
  fdevent_loop_t loop;
  std::function<void(fdevent_loop_t)> fn;
};

CXX_STATIC_ATTR
static
EVENT_HANDLER_DEFINE(_when_server_runs, ev_type, ev, ud) {
  (void) ev_type;
  (void) ev;
  RunningCallbackCtx *ctx = (RunningCallbackCtx *) ud;
  closesocket(ctx->send_sock);
  closesocket(ctx->recv_sock);
  ctx->fn(ctx->loop);
}

CXX_STATIC_ATTR
void
run_lockbox_webdav_server(std::shared_ptr<encfs::FsIO> fs_io,
                          encfs::Path root_path,
                          ipv4_t ipaddr,
                          port_t port,
                          const std::string & mount_name,
                          std::function<void(fdevent_loop_t)> when_done) {
  // create event loop (implemented by file descriptors)
  auto loop = fdevent_default_new();
  if (!loop) throw std::runtime_error("Couldn't create event loop");
  auto _free_loop = create_destroyer(loop, fdevent_destroy);

  struct sockaddr_in listen_addr;
  init_sockaddr_in(&listen_addr, ipaddr, port);

  // create network IO backend (implemented by the Socket API)
  auto network_io =
    http_backend_sockets_fdevent_new(loop,
                                     (struct sockaddr *) &listen_addr,
                                     sizeof(listen_addr));
  if (!network_io) throw std::runtime_error("Couldn't create network io");
  auto _destroy_network_io =
    create_destroyer(network_io, http_backend_sockets_fdevent_destroy);

  // create cfs for webdav backend
  const bool destroy_fs_on_delete = false;
  const auto cfs = fs_dynamic_new((fs_handle_t) fs_io.get(), &fsio_ops,
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
  build_uri_root << "http://localhost:" << port << "/";
  auto public_uri_root = std::move(build_uri_root).str();
  auto internal_root = "/" + mount_name;
  auto server = webdav_server_start(network_io, public_uri_root.c_str(),
                                    internal_root.c_str(), server_backend);
  if (!server) throw std::runtime_error("Couldn't start webdav server");

  // now set up callback
  fd_t sv[2];
  auto ret_socketpair = localhost_socketpair(sv);
  if (ret_socketpair) throw std::runtime_error("Couldnt create socketpair!");
  auto ret_send = send(sv[0], "1", 1, 0);
  if (ret_send == -1) {
    throw std::runtime_error("couldn't set up startup callback");
  }
  auto ctx = RunningCallbackCtx {sv[0], sv[1], loop, std::move(when_done)};
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

#ifndef _CXX_STATIC_BUILD
#undef CXX_STATIC_ATTR
#endif

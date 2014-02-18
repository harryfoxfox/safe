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

#ifndef __Safe__mount_win
#define __Safe__mount_win

#include <safe/win/ramdisk.hpp>
#include <safe/util.hpp>
#include <safe/win/util.hpp>
#include <safe/webdav_server.hpp>

#include <encfs/fs/FileUtils.h>
#include <encfs/fs/FsIO.h>
#include <encfs/cipher/MemoryPool.h>

#include <davfuse/util_sockets.h>

#include <iostream>
#include <string>

#include <safe/lean_windows.h>

namespace safe { namespace win {

typedef ManagedHandle ManagedThreadHandle;

inline
ManagedThreadHandle
create_managed_thread_handle(HANDLE a) {
  return ManagedThreadHandle(a);
}

enum class DriveLetter {
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
};

class MountDetails {
private:
  DriveLetter _drive_letter;
  std::string _name;
  ManagedThreadHandle _thread_handle;
  port_t _listen_port;
  encfs::Path _source_path;
  safe::WebdavServerHandle _ws;
  RAMDiskHandle _ramdisk_handle;

public:
  MountDetails(DriveLetter drive_letter,
               std::string name,
               ManagedThreadHandle thread_handle,
               port_t listen_port,
               encfs::Path source_path,
               safe::WebdavServerHandle ws,
               RAMDiskHandle ramdisk_handle)
    : _drive_letter(drive_letter)
    , _name(std::move(name))
    , _thread_handle(std::move(thread_handle))
    , _listen_port(listen_port)
    , _source_path(std::move(source_path))
    , _ws(std::move(ws))
    , _ramdisk_handle(std::move(ramdisk_handle)) {}

  const std::string &
  get_mount_name() const { return _name; }

  const encfs::Path &
  get_source_path() const { return _source_path; }

  const DriveLetter &
  get_drive_letter() const { return _drive_letter; }

  bool
  is_still_mounted() const;

  void
  signal_stop() const;

  void
  unmount();

  void
  open_mount() const;

  void
  disconnect_clients() const;
};

MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password);

}}

// DriveLetter helpers
namespace std {
  inline
  std::string
  to_string(safe::win::DriveLetter v) {
    return std::string(1, (int) v + 'A');
  }
}

template<class CharT, class Traits>
std::basic_ostream<CharT, Traits> &
operator<<(std::basic_ostream<CharT, Traits> & os,
           safe::win::DriveLetter dl) {
  return os << std::to_string(dl);
}


#endif

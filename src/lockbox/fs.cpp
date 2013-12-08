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

#include <davfuse/fs_dynamic.h>
#include <davfuse/fs_native.h>

#include <lockbox/CFsToFsIO.hpp>
#include <lockbox/SecureMemPasswordReader.hpp>
#include <lockbox/util.hpp>
#include <lockbox/UnicodeWrapperFsIO.hpp>

#include <lockbox/fs.hpp>

#include <sstream>

#ifndef _CXX_STATIC_BUILD
#define CXX_STATIC_ATTR
#endif

namespace lockbox {

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
  .path_is_valid = (fs_dynamic_path_is_valid_fn) fs_native_path_is_valid,
  .path_dirname = (fs_dynamic_path_dirname_fn) fs_native_path_dirname,
  .path_basename = (fs_dynamic_path_basename_fn) fs_native_path_basename,
  .path_join = (fs_dynamic_path_join_fn) fs_native_path_join,
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
  encfs_opts->rootDir = encrypted_folder_path;
  encfs_opts->passwordReader = std::make_shared<SecureMemPasswordReader>(std::move(password));

  // encfs
  auto encfs_io = std::make_shared<encfs::EncfsFsIO>();
  encfs_io->initFS(std::move(encfs_opts), cfg);

  return std::make_shared<UnicodeWrapperFsIO>(encfs_io, std::move(encrypted_folder_path));
}

}

#ifndef _CXX_STATIC_BUILD
#undef CXX_STATIC_ATTR
#endif

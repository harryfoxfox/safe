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

#include "fs_encfs.h"

#include "fs_encfs_fs.h"

#include <encfs/base/optional.h>
#include <encfs/fs/EncfsFsIO.h>

#include <memory>

// make sure our casts are okay

extern "C" {

encfs::EncfsFsIO *fs_handle_to_pointer(fs_encfs_t h) {
  static_assert(sizeof(fs_encfs_t) >= sizeof(encfs::EncfsFsIO *),
                "fs_encfs_handle_t cannot hold encfs::File");
  return (encfs::EncfsFsIO *) h;
}

encfs::File *file_handle_to_pointer(fs_encfs_file_handle_t h) {
  static_assert(sizeof(fs_encfs_file_handle_t) >= sizeof(encfs::File *),
                "fs_encfs_file_handle_t cannot hold encfs::File");
  return (encfs::File *) h;
}

encfs::Directory *directory_handle_to_pointer(fs_encfs_directory_handle_t h) {
  static_assert(sizeof(fs_encfs_directory_handle_t) >= sizeof(encfs::Directory *),
                "fs_encfs_directory_handle_t cannot hold encfs::File");
  return (encfs::Directory *) h;
}

fs_encfs_t pointer_to_fs_handle(encfs::EncfsFsIO *h) {
  return (fs_encfs_t) h;
}

fs_encfs_file_handle_t pointer_to_file_handle(encfs::File *h) {
  return (fs_encfs_file_handle_t) h;
}

fs_encfs_directory_handle_t pointer_to_directory_handle(encfs::Directory *h) {
  return (fs_encfs_directory_handle_t) h;
}

enum {
  DONT_CREATE = false,
  SHOULD_CREATE = true,
};

enum {
  NEED_WRITE = false,
  DONT_NEED_WRITE = true,
};


fs_encfs_t
fs_encfs_default_new(void) {
  // not supported, we need a file system to pass through to
  return NULL;
}

fs_encfs_t
fs_encfs_new(fs_t base_fs, const char *root_dir, const char *password) {
  auto opts = std::make_shared<encfs::EncFS_Opts>();

  opts->fs_io = std::make_shared<CFsToFsIO>(base_fs);
  opts->passwordReader = std::make_shared<StringPasswordReader>(password);
  opts->configMode = encfs::ConfigMode::Paranoia;
  opts->rootDir = root_dir;

  auto fs = new encfs::EncfsFsIO();
  try {
    fs->initFS(opts, opt::nullopt);
  }
  catch (...) {
    delete fs;
    // TODO: log error
    return NULL;
  }

  return pointer_to_fs_handle(fs);
}

fs_encfs_error_t
fs_encfs_open(fs_encfs_t fs_,
              const char *cpath, bool should_create,
              OUT_VAR fs_encfs_file_handle_t *handle,
              OUT_VAR bool *created) {
  auto fs = fs_handle_to_pointer(fs_);

  opt::optional<encfs::Path> path;
  try {
    path = fs->pathFromString(cpath);
  }
  catch (...) {
    return FS_ENCFS_ERROR_INVALID_ARG;
  }

  assert( path );

  if (created && should_create) {
    try {
      fs->get_attrs(*path);
      *created = false;
    }
    catch (const std::system_error & err) {
      if (err.code() != std::errc::no_such_file_or_directory) throw;
      // TODO: TOCTTOU bug here, between this point and when we actually open the file
      // this might be false
      *created = true;
    }
  }

  try {
    auto f = fs->openfile(std::move( *path ), NEED_WRITE, should_create);
    *handle = pointer_to_file_handle(new encfs::File(std::move(f)));
    return FS_ENCFS_ERROR_SUCCESS;
  }
  catch (...) {
    // TODO: return more specific error code
    return FS_ENCFS_ERROR_IO;
  }
}

fs_encfs_error_t
fs_encfs_fgetattr(fs_encfs_t fs, fs_encfs_file_handle_t file_handle,
                  OUT_VAR FsWin32Attrs *attrs);

fs_encfs_error_t
fs_encfs_ftruncate(fs_encfs_t fs, fs_encfs_file_handle_t file_handle,
                   fs_encfs_off_t offset);

fs_encfs_error_t
fs_encfs_read(fs_encfs_t fs, fs_encfs_file_handle_t file_handle,
              OUT_VAR char *buf, size_t size, fs_encfs_off_t off,
              OUT_VAR size_t *amt_read);

fs_encfs_error_t
fs_encfs_write(fs_encfs_t fs, fs_encfs_file_handle_t file_handle,
               const char *buf, size_t size, fs_encfs_off_t offset,
               OUT_VAR size_t *amt_written);

fs_encfs_error_t
fs_encfs_opendir(fs_encfs_t fs, const char *path,
                 OUT_VAR fs_encfs_directory_handle_t *dir_handle);

fs_encfs_error_t
fs_encfs_readdir(fs_encfs_t fs, fs_encfs_directory_handle_t dir_handle,
                 /* name is required and malloc'd by the implementation,
                    the user must free the returned pointer
                 */
                 OUT_VAR char **name,
                 /* attrs is optionally filled by the implementation */
                 OUT_VAR bool *attrs_is_filled,
                 OUT_VAR FsWin32Attrs *attrs);

fs_encfs_error_t
fs_encfs_closedir(fs_encfs_t fs, fs_encfs_directory_handle_t dir_handle);

/* can remove either a file or a directory,
   removing a directory should fail if it's not empty
*/
fs_encfs_error_t
fs_encfs_remove(fs_encfs_t fs, const char *path);

fs_encfs_error_t
fs_encfs_mkdir(fs_encfs_t fs, const char *path);

fs_encfs_error_t
fs_encfs_getattr(fs_encfs_t fs, const char *path,
                 OUT_VAR FsWin32Attrs *attrs);

fs_encfs_error_t
fs_encfs_rename(fs_encfs_t fs,
                const char *src, const char *dst);

fs_encfs_error_t
fs_encfs_close(fs_encfs_t fs, fs_encfs_file_handle_t handle);

bool
fs_encfs_destroy(fs_encfs_t fs);

bool
fs_encfs_path_is_root(fs_encfs_t fs, const char *path);

bool
fs_encfs_path_equals(fs_encfs_t fs, const char *a, const char *b);

bool
fs_encfs_path_is_parent(fs_encfs_t fs,
                        const char *potential_parent,
                        const char *potential_child);

const char *
fs_encfs_path_sep(fs_encfs_t fs);

}

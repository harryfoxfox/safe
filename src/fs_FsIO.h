/*
  Lockbox: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lessage General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef _FS_FSIO_H
#define _FS_FSIO_H

#ifdef __cplusplus

#include <encfs/fs/FsIO.h>

#include <climits>
#include <cstddef>

typedef encfs::FsIO *fs_FsIO_t;
typedef encfs::DirectoryIO *fs_FsIO_directory_handle_t;
typedef encfs::FileIO *fs_FsIO_file_handle_t;

#else

#include <limits.h>
#include <stddef.h>

struct _fs_FsIO_handle;
struct _fs_FsIO_directory_handle;
struct _fs_FsIO_file_handle;

typedef struct _fs_FsIO_handle *fs_FsIO_t;
typedef struct _fs_FsIO_directory_handle *fs_FsIO_directory_handle_t;
typedef struct _fs_FsIO_file_handle *fs_FsIO_file_handle_t;

#endif

#define OUT_VAR

#ifdef __cplusplus
extern "C" {
#endif

/* non-opaque structures */
typedef enum {
  FS_FSIO_ERROR_SUCCESS,
  FS_FSIO_ERROR_DOES_NOT_EXIST,
  FS_FSIO_ERROR_NOT_DIR,
  FS_FSIO_ERROR_IS_DIR,
  FS_FSIO_ERROR_IO,
  FS_FSIO_ERROR_NO_SPACE,
  FS_FSIO_ERROR_PERM,
  FS_FSIO_ERROR_EXISTS,
  FS_FSIO_ERROR_CROSS_DEVICE,
  FS_FSIO_ERROR_NO_MEM,
  FS_FSIO_ERROR_INVALID_ARG,
} fs_FsIO_error_t;

/* it would be nice to sync these with FsIO.h,
   maybe there should be <encfs/fs/cfstype.h */
typedef intmax_t fs_FsIO_time_t;
typedef intmax_t fs_FsIO_off_t;
typedef uintmax_t fs_FsIO_file_id_t;

/* NB: not totally sure about defining constants like this,
   a #define might be better */
static const fs_FsIO_time_t FS_FSIO_INVALID_TIME = INTMAX_MAX;
static const fs_FsIO_off_t FS_FSIO_INVALID_OFF = INTMAX_MAX;

typedef struct {
  fs_FsIO_time_t modified_time;
  fs_FsIO_time_t created_time;
  bool is_directory;
  fs_FsIO_off_t size;
  fs_FsIO_file_id_t file_id;
} FsFsIOAttrs;

fs_FsIO_error_t
fs_FsIO_open(fs_FsIO_t fs,
             const char *path, bool create,
             OUT_VAR fs_FsIO_file_handle_t *handle,
             OUT_VAR bool *created);

fs_FsIO_error_t
fs_FsIO_fgetattr(fs_FsIO_t fs, fs_FsIO_file_handle_t file_handle,
                 OUT_VAR FsFsIOAttrs *attrs);

fs_FsIO_error_t
fs_FsIO_ftruncate(fs_FsIO_t fs, fs_FsIO_file_handle_t file_handle,
                  fs_FsIO_off_t offset);

fs_FsIO_error_t
fs_FsIO_read(fs_FsIO_t fs, fs_FsIO_file_handle_t file_handle,
             OUT_VAR char *buf, size_t size, fs_FsIO_off_t off,
             OUT_VAR size_t *amt_read);

fs_FsIO_error_t
fs_FsIO_write(fs_FsIO_t fs, fs_FsIO_file_handle_t file_handle,
              const char *buf, size_t size, fs_FsIO_off_t off,
              OUT_VAR size_t *amt_written);

fs_FsIO_error_t
fs_FsIO_close(fs_FsIO_t fs, fs_FsIO_file_handle_t handle);

fs_FsIO_error_t
fs_FsIO_opendir(fs_FsIO_t fs, const char *path,
                OUT_VAR fs_FsIO_directory_handle_t *dir_handle);

fs_FsIO_error_t
fs_FsIO_readdir(fs_FsIO_t fs, fs_FsIO_directory_handle_t dir_handle,
                /* name is required and malloc'd by the implementation,
                   the user must free the returned pointer
                */
                OUT_VAR char **name,
                /* attrs is optionally filled by the implementation */
                OUT_VAR bool *attrs_is_filled,
                OUT_VAR FsFsIOAttrs *attrs);

fs_FsIO_error_t
fs_FsIO_closedir(fs_FsIO_t fs, fs_FsIO_directory_handle_t dir_handle);

/* can remove either a file or a directory,
   removing a directory should fail if it's not empty
*/
fs_FsIO_error_t
fs_FsIO_remove(fs_FsIO_t fs, const char *path);

fs_FsIO_error_t
fs_FsIO_mkdir(fs_FsIO_t fs, const char *path);

fs_FsIO_error_t
fs_FsIO_getattr(fs_FsIO_t fs, const char *path,
                OUT_VAR FsFsIOAttrs *attrs);

fs_FsIO_error_t
fs_FsIO_rename(fs_FsIO_t fs,
               const char *src, const char *dst);

bool
fs_FsIO_path_is_root(fs_FsIO_t fs, const char *path);

bool
fs_FsIO_path_equals(fs_FsIO_t fs, const char *a, const char *b);

bool
fs_FsIO_path_is_parent(fs_FsIO_t fs,
                       const char *potential_parent,
                       const char *potential_child);

const char *
fs_FsIO_path_sep(fs_FsIO_t fs);


#ifdef __cplusplus
}
#endif

#undef OUT_VAR

#endif

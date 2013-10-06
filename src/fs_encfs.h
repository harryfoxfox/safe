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

#ifndef _FS_ENCFS_H
#define _FS_ENCFS_H

#include <limits.h>
#include <stddef.h>

#define OUT_VAR

#ifdef __cplusplus
extern "C" {
#endif

struct _fs_encfs_handle;
struct _fs_encfs_directory_handle;
struct _fs_encfs_file_handle;

typedef struct _fs_encfs_handle *fs_encfs_t;
typedef struct _fs_encfs_file_handle *fs_encfs_file_handle_t;
typedef struct _fs_encfs_directory_handle *fs_encfs_directory_handle_t;

/* non-opaque structures */
typedef enum {
  FS_ENCFS_ERROR_SUCCESS,
  FS_ENCFS_ERROR_DOES_NOT_EXIST,
  FS_ENCFS_ERROR_NOT_DIR,
  FS_ENCFS_ERROR_IS_DIR,
  FS_ENCFS_ERROR_IO,
  FS_ENCFS_ERROR_NO_SPACE,
  FS_ENCFS_ERROR_PERM,
  FS_ENCFS_ERROR_EXISTS,
  FS_ENCFS_ERROR_CROSS_DEVICE,
  FS_ENCFS_ERROR_NO_MEM,
  FS_ENCFS_ERROR_INVALID_ARG,
} fs_encfs_error_t;

typedef long long fs_encfs_time_t;
typedef unsigned long long fs_encfs_off_t;

/* NB: not totally sure about defining constants like this,
   a #define might be better */
static const fs_encfs_time_t FS_ENCFS_INVALID_TIME = LLONG_MAX;
static const fs_encfs_off_t FS_ENCFS_INVALID_OFF = ULLONG_MAX;

typedef struct {
  fs_encfs_time_t modified_time;
  fs_encfs_time_t created_time;
  bool is_directory;
  fs_encfs_off_t size;
} FsWin32Attrs;

fs_encfs_t
fs_encfs_default_new(void);

fs_encfs_error_t
fs_encfs_open(fs_encfs_t fs,
              const char *path, bool create,
              OUT_VAR fs_encfs_file_handle_t *handle,
              OUT_VAR bool *created);

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

#ifdef __cplusplus
}
#endif

#undef OUT_VAR

#endif

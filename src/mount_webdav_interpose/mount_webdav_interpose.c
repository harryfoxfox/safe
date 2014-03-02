/*
 Safe: Encrypted File System
 Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>
 
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

#define _ISOC99_SOURCE

#include <mount_webdav_interpose/mount_webdav_interpose.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sandbox.h>
#include <crt_externs.h>
#include <errno.h>

#include <unistd.h>

#include <sys/param.h>
#include <sys/mount.h>

static
bool
str_startswith(char *str, char *prefix) {
  return strstr(str, prefix) == str;
}

int
execve(const char *path, char *const argv[], char *const envp[]) {
  /* carry over all DYLD_* env variables */

  /* get handle to original execve() */
  int (*original_execve)(const char *, char *const [], char *const []) =
    (int (*)(const char *, char *const [], char *const [])) dlsym(RTLD_NEXT, "execve");
  if (!original_execve) return -1;

  /* get environ */
  char ***environ__ = _NSGetEnviron();
  if (!environ__) return -1;
  char **environ_ = *environ__;

  /* num of DYLD_* env variables */
  size_t num_dyld_entries = 0;
  for (size_t i = 0; environ_[i]; ++i) {
    if (str_startswith(environ_[i], "DYLD_") ||
        str_startswith(environ_[i], "SAFE_")) {
      num_dyld_entries++;
    }
  }

  /* get num of passed in env variables */
  size_t num_of_envp_entries;
  for (num_of_envp_entries = 0; envp[num_of_envp_entries]; ++num_of_envp_entries);

  char **new_envp = malloc(sizeof(envp[0]) * (num_of_envp_entries + num_dyld_entries + 1));
  if (!new_envp) return -1;

  /* copy over original envp */
  memcpy(new_envp, envp, sizeof(envp[0]) * num_of_envp_entries);

  /* copy over carried over DYLD_* env variables */
  char **to_copy_envp = new_envp + num_of_envp_entries;
  for (size_t i = 0; environ_[i]; ++i) {
    if (str_startswith(environ_[i], "DYLD_") ||
        str_startswith(environ_[i], "SAFE_")) {
      *to_copy_envp++ = environ_[i];
    }
  }

  /* null terminate */
  *to_copy_envp = 0;

  int toret = original_execve(path, argv, new_envp);
  free(new_envp);
  return toret;
}

int
sandbox_init(const char *profile, uint64_t flags, char **errorbuf) {
  /* allow access to ramdisk */

  /* get handle to original sandbox_init() */
  int (*original_sandbox_init)(const char *, uint64_t, char **) =
    (int (*)(const char *, uint64_t, char **)) dlsym(RTLD_NEXT, "sandbox_init");
  if (!original_sandbox_init) {
    //fprintf(stderr, "no original sandbox!\n");
    return -1;
  }

  char *mount_root = getenv(SAFE_RAMDISK_MOUNT_ROOT_ENV);
  if (!mount_root) {
    //fprintf(stderr, "no mount root!\n");
    return original_sandbox_init(profile, flags, errorbuf);
  }

  /* create a new sb that allows access to ramdisk */
  char *new_sb_string = NULL;
  int new_sb_string_len = -1;
  if (flags == SANDBOX_NAMED) {
    size_t l = strlen(profile);
    new_sb_string_len =
      asprintf(&new_sb_string,
               "(version 1)\n"
               "(deny default)\n"
               "(import \"%s%s\")\n"
               "\n"
               "(allow file* (regex #\"%s(/.+)?\"))\n"
               "\n", profile,
               strcmp(&profile[l - 3], ".sb") == 0 ? "" : ".sb",
               mount_root);
  }
  else {
    new_sb_string_len =
      asprintf(&new_sb_string,
               "%s\n"
               "(allow file* (regex #\"%s(/.+)?\"))\n",
               profile, mount_root);
  }

  int toret = -1;

  if (new_sb_string_len < 0) {
    //    fprintf(stderr, "asprintf failed %s\n", strerror(errno));
    goto error;
  }

  toret = original_sandbox_init(new_sb_string, 0, errorbuf);

 error:
  free(new_sb_string);

  return toret;
}

int
mkstemp(char *template) {
  int (*original_mkstemp)(char *) =
    (int (*)(char *)) dlsym(RTLD_NEXT, "mkstemp");
  if (!original_mkstemp) return -1;

  char *mount_root = getenv(SAFE_RAMDISK_MOUNT_ROOT_ENV);
  if (mount_root && strstr(template, ".webdavcache.")) {
    /* check mount dev is the same */
    struct statfs mount_fs_stat;
    int ret = statfs(mount_root, &mount_fs_stat);
    if (ret < 0) return -1;

    char *mount_device = getenv(SAFE_RAMDISK_MOUNT_DEV_ENV);
    if (!mount_device || strcmp(mount_fs_stat.f_mntfromname, mount_device)) return -1;

    /* change location to be the ramdisk */
    snprintf(template, MAXPATHLEN, "%s/webdav.XXXXXX", mount_root);
  }

  return original_mkstemp(template);
}


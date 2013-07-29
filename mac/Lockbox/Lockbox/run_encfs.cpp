/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003-2004, Valient Gough
 *
 * This library is free software; you can distribute it and/or modify it under
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GPL in the file COPYING for more
 * details.
 *
 */

#include "fs/encfs.h"

#include <iostream>
#include <string>
#include <sstream>

#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>

#include <glog/logging.h>

#include "base/config.h"
#include "base/autosprintf.h"
#include "base/ConfigReader.h"
#include "base/Error.h"
#include "base/Interface.h"
#include "base/i18n.h"

#include "cipher/CipherV1.h"

#include "fs/FileUtils.h"
#include "fs/DirNode.h"
#include "fs/Context.h"

#include "run_encfs.h"

using namespace std;
using namespace encfs;

static struct fuse_operations encfs_oper = {
  .getattr = encfs_getattr,
  .fgetattr = encfs_fgetattr,
  .getdir = encfs_getdir,
  .mknod = encfs_mknod,
  .mkdir = encfs_mkdir,
  .unlink = encfs_unlink,
  .rmdir = encfs_rmdir,
  .rename = encfs_rename,
  .open = encfs_open,
  .read = encfs_read,
  .write = encfs_write,
  .release = encfs_release,
};

bool
run_encfs(const char *encrypted_path, char *password) {
  FLAGS_logtostderr = 1;
  FLAGS_minloglevel = 0; // DEBUG and above.

  google::InitGoogleLogging("encfs");
  google::InstallFailureSignalHandler();

  CipherV1::init(true);

  // context is not a smart pointer because it will live for the life of
  // the filesystem.
  string encrypted_path_str(encrypted_path);
  shared_ptr<EncFS_Opts> opts = shared_ptr<EncFS_Opts>(new EncFS_Opts());
  opts->configMode = Config_Paranoia;
  opts->rootDir = encrypted_path_str;
  opts->rawPass = password;

  EncFS_Context *ctx = new EncFS_Context();
  ctx->publicFilesystem = opts->ownerCreate;
  RootPtr rootInfo = initFS(ctx, opts);

  int returnCode = EXIT_FAILURE;

  if (rootInfo) {
    // set the globally visible root directory node
    ctx->setRoot(rootInfo->root);
    ctx->args = shared_ptr<EncFS_Args>();
    ctx->opts = opts;

    // reset umask now, since we don't want it to interfere with the
    // pass-thru calls..
    umask(0);

    try {
      // fuse_main returns an error code in newer versions of fuse..
      int argc = 2;
      char arg0[] = "encfs";
      char arg1[] = "-s";
      char *argv[2] = {arg0, arg1};
      int res = fuse_main(argc, argv, &encfs_oper, (void *) ctx);
      if (!res) {
        returnCode = EXIT_SUCCESS;
      }

    }
    catch(std::exception &ex) {
      LOG(ERROR) << "Internal error: Caught exception from main loop: "
                 << ex.what();
    }
    catch(...) {
      LOG(ERROR) << "Internal error: Caught unexpected exception";
    }
  }

  // cleanup so that we can check for leaked resources..
  rootInfo.reset();
  ctx->setRoot( shared_ptr<DirNode>() );

  CipherV1::shutdown(true);

  return returnCode;
}

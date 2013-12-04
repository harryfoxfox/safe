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

#include <lockbox/mount_lockbox_dialog_logic.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/cipher/MemoryPool.h>
#include <encfs/base/optional.h>

#include <memory>
#include <string>

namespace lockbox {

opt::optional<decltype(make_error_message("", ""))>
verify_mount_lockbox_dialog_fields(const std::shared_ptr<encfs::FsIO> & fs,
                                   const std::string & location,
                                   const encfs::SecureMem & password) {
  (void) password;

  opt::optional<encfs::Path> maybe_location_path;
  try {
    maybe_location_path = fs->pathFromString(location);
  }
  catch (...) {
    return make_error_message("Bad Location",
                              "The location you chose is not a valid path.");
  }

  auto encrypted_container_path = *maybe_location_path;

  auto is_dir = false;
  try {
    is_dir = encfs::is_directory(fs, encrypted_container_path);
  }
  catch (...) {
    // TODO: log error?
  }

  if (!is_dir) {
    return make_error_message("Bad Location",
                              "The location you chose is not a folder.");
  }

  return opt::nullopt;
}

}

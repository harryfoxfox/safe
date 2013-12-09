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

#ifndef __lockbox_windows_mount_lockbox_dialog_hpp
#define __lockbox_windows_mount_lockbox_dialog_hpp

#include <lockbox/mount_win.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/base/optional.h>

#include <memory>

#include <lockbox/lean_windows.h>

namespace lockbox { namespace win {

typedef std::function<opt::optional<lockbox::win::MountDetails>(const encfs::Path &)> TakeMountFn;

opt::optional<lockbox::win::MountDetails>
mount_existing_lockbox_dialog(HWND owner, std::shared_ptr<encfs::FsIO> fsio,
                              TakeMountFn take_mount,
                              opt::optional<encfs::Path> path);

}}

#endif

/*
  Safe: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

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

#ifndef __safe_windows_create_safe_dialog_hpp
#define __safe_windows_create_safe_dialog_hpp

#include <safe/win/mount.hpp>

#include <encfs/fs/FsIO.h>

#include <encfs/base/optional.h>

#include <safe/lean_windows.h>

namespace safe { namespace win {

opt::optional<safe::win::MountDetails>
create_new_safe_dialog(HWND owner, std::shared_ptr<encfs::FsIO> fsio);

}}

#endif

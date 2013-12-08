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

#ifndef __create_lockbox_dialog_logic_hpp
#define __create_lockbox_dialog_logic_hpp

#include <lockbox/util.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/cipher/MemoryPool.h>
#include <encfs/base/optional.h>

#include <memory>
#include <string>

namespace lockbox {

opt::optional<decltype(make_error_message("", ""))>
verify_create_lockbox_dialog_fields(const std::shared_ptr<encfs::FsIO> & fs,
                                    const std::string & location,
                                    const std::string & name,
                                    const encfs::SecureMem & password,
                                    const encfs::SecureMem & password_confirm);

}

#endif

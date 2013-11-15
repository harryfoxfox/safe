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

// Takes an FsIO that performs path lookups using byte-wise comparison
// and turns it into a unicode path-aware file system file system 

#ifndef _unicode_wrapper_fsio_hpp
#define _unicode_wrapper_fsio_hpp

#include <encfs/fs/FsIO.h>

namespace lockbox {

class UnicodeWrapperFsIO : public encfs::FsIO {
  std::shared_ptr<encfs::FsIO> _base;
  encfs::Path _conversion_root;

private:
  encfs::Path _convertPath(const encfs::Path & path) const;

public:
  UnicodeWrapperFsIO(std::shared_ptr<encfs::FsIO> base, encfs::Path conversion_root)
    : _base(std::move(base))
    , _conversion_root(std::move(conversion_root)) {}
  virtual ~UnicodeWrapperFsIO() override = default;

  // generic fs methods
  virtual encfs::Path pathFromString(const std::string & path) const override;

  virtual encfs::Directory opendir(const encfs::Path & path) const override;
  virtual encfs::File openfile(const encfs::Path & path,
                               bool open_for_write = false,
                               bool create = false) override;

  virtual void mkdir(const encfs::Path & path) override;

  virtual void rename(const encfs::Path & pathSrc, const encfs::Path & pathDst) override;

  virtual void unlink(const encfs::Path & path) override;
  virtual void rmdir(const encfs::Path & path) override;

  virtual encfs::FsFileAttrs get_attrs(const encfs::Path & path) const override;
  virtual void set_times(const encfs::Path & path,
                         const opt::optional<encfs::fs_time_t> & atime,
                         const opt::optional<encfs::fs_time_t> & mtime) override;
};

}

#endif


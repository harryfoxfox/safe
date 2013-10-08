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

#include "CFsToFsIO.h"

#include "c_fs_to_fs_io_fs.h"

#include <encfs/fs/FsIO.h>

class fs_error : public std::exception {
public:
  fs_error( fs_error_t /*err*/ ) {}
};

class CFsToFsIOPath final : public encfs::StringPath<fs_encfs_path_sep> {
};

class CFsToFsIODirectoryIO final : public encfs::DirectoryIO {
private:
  fs_t _fs;
  fs_directory_handle_t _dh;

  CFsToFsIODirectoryIO(fs_t fs, directory_handle_t dh) noexcept
    : _fs(fs), _dh(dh) {}

public:
  virtual ~CFsToFsIODirectoryIO() override;
  virtual opt::optional<encfs::FsDirEnt> readdir() override;

  friend class CFsToFsIO;
};

class CFsToFsIOFileIO : public encfs::FileIO {
  fs_t _fs;
  fs_file_handle_t _fh;
  bool _open_for_write;

public:
  CFsToFsIOFileIO(fs_t fs, fs_file_handle_t fh, bool open_for_write)
    : _fs(fs)
    , _fh(fh)
    , _open_for_write(open_for_write) {}

  // can't copy CFsToFsIOFileIO unless we dup fd
  CFsToFsIOFileIO(const CFsToFsIOFileIO &) = delete;
  CFsToFsIOFileIO &operator=(const CFsToFsIOFileIO &) = delete;

  virtual ~CFsToFsIOFileIO() override;

  virtual Interface interface() const override;

  virtual FsFileAttrs get_attrs() const override;

  virtual size_t read(const encfs::IORequest & req) const override;
  virtual void write(const encfs::IORequest &req) override;

  virtual void truncate( encfs::fs_off_t size ) override;

  virtual bool isWritable() const override;

  virtual void sync(bool datasync) override;
};

CFsToFsIODirectoryIO::~CFsToFsIODirectoryIO() {
  auto err = fs_closedir(_fs, _dh);
  if (err) log_error("couldn't close directory handle, leaking...");
}

opt::optional<encfs::FsDirEnt> CFsToFsIODirectoryIO::readdir() {
  char *name;
  bool attrs_is_filled;
  FsAttrs attrs;
  auto err = fs_readdir(_fs, _dh, &name, &attrs_is_filled, &attrs);
  if (err) throw fs_error( err );

  if (!name) return opt::nullopt;

  auto toret = encfs::FsDirEnt(name);
  free(name);

  if (attrs_is_filled) {
    toret.file_id = attrs.file_id;
    toret.type = attrs.is_directory
      ? FsFileType::DIRECTORY
      : FsFileType::REGULAR;
  }

  return std::move(toret);
}

CFsToFsIOFileIO::~CFsToFsIOFileIO() {
  auto err = fs_close(_fs, _fh);
  if (err) log_error("couldn't close file handle, leaking...");
}

static Interface CFsToFsIOFileIO_iface = makeInterface("FileIO/CFsToFsIO", 1, 0, 0);

Interface CFsToFsIOFileIO::interface() const {
  return CFsToFsIOFileIO_iface;
}

encfs::FsFileAttrs CFsToFsIOFileIO::get_attrs() const {
  FsAttrs attrs;
  auto err = fs_fgetattr(_fs, _fh, &attrs);
  if (err) throw fs_error(err);

  return (FsFileAttrs) {
    .type = attrs.is_directory ? FsFileType::DIRECTORY : FsFileType::REGULAR,
    .mtime = attrs.modified_time,
    .size = attrs.size,
   };
}

size_t CFsToFsIOFileIO::read(const IORequest & req) const {
}

void CFsToFsIOFileIO::write(const IORequest &req) {
}

void CFsToFsIOFileIO::truncate( fs_off_t size ) {
}

bool CFsToFsIOFileIO::isWritable() const  {
}

void CFsToFsIOFileIO::sync(bool datasync) {
}

CFsToFsIO::CFsToFsIO(fs_t fs_)
  : fs(fs_) {
}

Path CFsToFsIO::pathFromString(const std::string &path) const {
  // in the fs C interface, paths are always UTF-8 encoded strings
  // TODO: check UTF-8 validity
  return CFsToFsIOPath(fs_encfs_path_is_root(fs, path.c_str()), path);
}

Directory CFsToFsIO::opendir(const Path &path) const {
  fs_directory_handle_t dh;
  auto err = fs_opendir(fs, path.c_str(), &dh);
  if (err) throw fs_error( err );
  return CFsToFsIODirectoryIO(dh);
}

File openfile(const Path &path,
              bool open_for_write,
              bool create) {
  fs_file_handle_t fh;
  auto err = fs_open(fs, path.c_str(), create, &fh, NULL);
  if (err) throw fs_error( err );
  return CFsToFsIOFileIO(fh, open_for_write);
}

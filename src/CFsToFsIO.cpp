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
#include <encfs/base/Interface.h>
#include <encfs/base/optional.h>

#include <davfuse/logging.h>

#include <limits>

#include <cstring>

namespace lockbox {

class fs_error : public std::exception {
public:
  fs_error(fs_error_t /*err*/) {}
};

class CFsToFsIOPath final : public encfs::StringPathDynamicSep {
protected:
  fs_t _fs;

  virtual std::shared_ptr<encfs::PathPoly> _from_string(std::string str) const
  {
    return std::make_shared<CFsToFsIOPath>( _fs, std::move( str ) );
  }

public:
  CFsToFsIOPath(fs_t fs, std::string str)
    : StringPathDynamicSep( fs_path_sep(fs), std::move( str ) )
    , _fs( fs ) {
    // TODO: support this
    assert(strlen(fs_path_sep(fs)) != 1);
  }

  virtual encfs::Path dirname() const override {
    if (fs_path_is_root(_fs, _path.c_str())) return _from_string(_path);

    /* do this */
    auto last = _path.rfind(_sep);
    assert(last != std::string::npos);
    return _from_string(_path.substr(0, last));
  }

  virtual bool is_root() const {
    return fs_path_is_root(_fs, _path.c_str());
  }
};

class CFsToFsIODirectoryIO final : public encfs::DirectoryIO {
private:
  fs_t _fs;
  fs_directory_handle_t _dh;

  CFsToFsIODirectoryIO(fs_t fs, fs_directory_handle_t dh) noexcept
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

  virtual encfs::Interface interface() const override;

  virtual encfs::FsFileAttrs get_attrs() const override;

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
  char *cname;
  bool attrs_is_filled;
  FsAttrs attrs;
  auto err = fs_readdir(_fs, _dh, &cname, &attrs_is_filled, &attrs);
  if (err) throw fs_error( err );

  if (!cname) return opt::nullopt;

  auto name = std::string(cname);
  free(cname);

  // TODO: fix this, make fs_readdir interface congruent
  //       to DirectoryIO::readdir interface
  //       (i.e. make file_id required in both or neither)
  if (!attrs_is_filled) throw std::runtime_error("bad readdir");

  return encfs::FsDirEnt(std::string(name), attrs.file_id,
                         attrs.is_directory
                         ? encfs::FsFileType::DIRECTORY
                         : encfs::FsFileType::REGULAR);
}

CFsToFsIOFileIO::~CFsToFsIOFileIO() {
  auto err = fs_close(_fs, _fh);
  if (err) log_error("couldn't close file handle, leaking...");
}

static encfs::Interface CFsToFsIOFileIO_iface = encfs::makeInterface("FileIO/CFsToFsIO", 1, 0, 0);

encfs::Interface CFsToFsIOFileIO::interface() const {
  return CFsToFsIOFileIO_iface;
}

encfs::FsFileAttrs CFsToFsIOFileIO::get_attrs() const {
  FsAttrs attrs;
  auto err = fs_fgetattr(_fs, _fh, &attrs);
  if (err) throw fs_error(err);

  return (encfs::FsFileAttrs) {
    .type = (attrs.is_directory ?
             encfs::FsFileType::DIRECTORY :
             encfs::FsFileType::REGULAR),
    .mtime = attrs.modified_time,
    .size = attrs.size,
    .file_id = attrs.file_id,
    .posix = opt::nullopt
   };
}

size_t CFsToFsIOFileIO::read(const encfs::IORequest & req) const {
  size_t amt_read;
  auto err = fs_read(_fs, _fh, (char *) req.data,
                     req.dataLen, req.offset, &amt_read);
  if (err) throw fs_error(err);
  return amt_read;
}

void CFsToFsIOFileIO::write(const encfs::IORequest &req) {
  if (!_open_for_write) throw fs_error(FS_POSIX_ERROR_PERM);

  size_t written = 0;
  while (written != req.dataLen) {
    size_t amt;
    auto err = fs_write(_fs, _fh, (char *) req.data + written,
                        req.dataLen - written, req.offset + written,
                        &amt);
    // NB: this is bad to fail a write() in the middle
    // we should change the interface of FileIO::write to
    // allow partial writes
    if (err) throw fs_error(err);

    written += amt;
  }
}

void CFsToFsIOFileIO::truncate(encfs::fs_off_t size) {
  if (!_open_for_write) throw fs_error(FS_POSIX_ERROR_PERM);
  auto err = fs_ftruncate(_fs, _fh, size);
  if (err) throw fs_error(err);
}

bool CFsToFsIOFileIO::isWritable() const  {
  return _open_for_write;
}

void CFsToFsIOFileIO::sync(bool /*datasync*/) {
  // C Fs does not support this interface
}

CFsToFsIO::CFsToFsIO(fs_t fs)
  : _fs(fs) {
}

const std::string &CFsToFsIO::path_sep() const {
  static const std::string _path_sep = fs_path_sep(_fs);
  return _path_sep;
}

encfs::Path CFsToFsIO::pathFromString(const std::string &path) const {
  // in the fs C interface, paths are always UTF-8 encoded strings
  // TODO: check UTF-8 validity
  return std::make_shared<CFsToFsIOPath>(_fs, path);
}

encfs::Directory CFsToFsIO::opendir(const encfs::Path &path) const {
  fs_directory_handle_t dh;
  auto err = fs_opendir(_fs, path.c_str(), &dh);
  if (err) throw fs_error(err);
  return std::unique_ptr<CFsToFsIODirectoryIO>(new CFsToFsIODirectoryIO(_fs, dh));
}

encfs::File CFsToFsIO::openfile(const encfs::Path &path,
                                bool open_for_write,
                                bool create) {
  fs_file_handle_t fh;
  auto err = fs_open(_fs, path.c_str(), create, &fh, NULL);
  if (err) throw fs_error(err);
  return std::unique_ptr<CFsToFsIOFileIO>(new CFsToFsIOFileIO(_fs, fh, open_for_write));
}

void CFsToFsIO::mkdir(const encfs::Path &path) {
  auto err = fs_mkdir( _fs, path.c_str() );
  if (err) throw fs_error(err);
}

void CFsToFsIO::rename(const encfs::Path &pathSrc, const encfs::Path &pathDst) {
  auto err = fs_rename( _fs, pathSrc.c_str(), pathDst.c_str() );
  if (err) throw fs_error(err);
}

void CFsToFsIO::unlink(const encfs::Path &path) {
  auto err = fs_remove( _fs, path.c_str() );
  if (err) throw fs_error(err);
}

void CFsToFsIO::rmdir(const encfs::Path &path) {
  auto err = fs_remove( _fs, path.c_str() );
  if (err) throw fs_error(err);
}

void CFsToFsIO::set_times(const encfs::Path &path,
                          const opt::optional<encfs::fs_time_t> &atime,
                          const opt::optional<encfs::fs_time_t> &mtime) {
  if(atime &&
     (*atime > std::numeric_limits<::fs_time_t>::max() ||
      *mtime < std::numeric_limits<::fs_time_t>::lowest())) {
    throw fs_error(FS_ERROR_INVALID_ARG);
  }

  if(mtime &&
     (*mtime > std::numeric_limits<::fs_time_t>::max() ||
      *mtime < std::numeric_limits<::fs_time_t>::lowest())) {
    throw fs_error(FS_ERROR_INVALID_ARG);
  }

  ::fs_time_t atime_in = atime ? (::fs_time_t) *atime : FS_INVALID_TIME;
  ::fs_time_t mtime_in = mtime ? (::fs_time_t) *mtime : FS_INVALID_TIME;

  auto err = fs_set_times(_fs, path.c_str(), atime_in, mtime_in);
  if (err) throw fs_error(err);
}

}

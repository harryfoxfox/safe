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

#include <lockbox/UnicodeWrapperFsIO.hpp>

#include <lockbox/unicode_fs.hpp>
#include <lockbox/util.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/base/optional.h>

namespace lockbox {

class UnicodeWrapperDirectoryIO : public encfs::DirectoryIO {
  encfs::Directory _dir;

public:
  UnicodeWrapperDirectoryIO(encfs::Directory dir)
    : _dir(std::move(dir)) {}
  virtual ~UnicodeWrapperDirectoryIO() override = default;
  virtual opt::optional<encfs::FsDirEnt> readdir() override {
    opt::optional<encfs::FsDirEnt> toret;
    while (!toret) {
      toret = _dir.readdir();
      if (!toret) return toret;
      if (!lockbox::unicode_fs::is_normalized_path_component(toret->name)) {
        toret = opt::nullopt;
      }
    }
    toret->name = lockbox::unicode_fs::normalize_path_component_for_user(toret->name);
    return std::move(toret);
  }
};

encfs::Path
UnicodeWrapperFsIO::_convertPath(const encfs::Path & path) const {
  // collect all path components in order
  auto root = path;
  auto comps = std::vector<std::string>();
  while (root != _conversion_root) {
    comps.push_back(root.basename());
    root = root.dirname();
  }

  std::reverse(std::begin(comps), std::end(comps));

  for (const auto & p : comps) {
    // normalized p
    auto nfc_p = lockbox::unicode_fs::normalize_path_component_for_fs(p);

    // find the canonical string of this path component "p"
    // in the parent Directory
    opt::optional<encfs::Directory> maybe_dir;
    try {
      maybe_dir = _base->opendir(root);
    }
    catch (const std::system_error &err) {
      if (err.code() != std::errc::no_such_file_or_directory) throw;
    }

    auto similar_files = std::vector<std::string>();
    if (maybe_dir) {
      opt::optional<encfs::FsDirEnt> ent;
      while ((ent = maybe_dir->readdir())) {
        if (!lockbox::unicode_fs::is_normalized_path_component(ent->name)) {
          // skip bad path
          continue;
        }
        if (lockbox::unicode_fs::normalized_path_components_equal(ent->name, nfc_p)) {
          similar_files.push_back(std::move(ent->name));
          break;
        }
      }
    }

    // didn't find a canonical file name for the current
    // path component, treat this component as the canonical version
    opt::optional<std::string> canon_name;
    if (similar_files.empty()) canon_name = std::move(nfc_p);
    else {
      // NB: we do this so we always select the same files
      //     for the canonical version since there is no guarantee of the
      //     ordering of readdir()
      std::sort(std::begin(similar_files), std::end(similar_files));
      canon_name = std::move(similar_files.front());
    }

    root = root.join(std::move(*canon_name));
  }

  return std::move(root);
}

encfs::Path
UnicodeWrapperFsIO::pathFromString(const std::string & path) const {
  return _base->pathFromString(path);
}

encfs::Directory
UnicodeWrapperFsIO::opendir(const encfs::Path & path) const {
  return lockbox::make_unique<UnicodeWrapperDirectoryIO>(_base->opendir(_convertPath(path)));
}

encfs::File
UnicodeWrapperFsIO::openfile(const encfs::Path & path,
                             bool open_for_write,
                             bool create) {
  return _base->openfile(_convertPath(path),
                         open_for_write,
                         create);
}

void
UnicodeWrapperFsIO::mkdir(const encfs::Path & path) {
  return _base->mkdir(_convertPath(path));
}

void
UnicodeWrapperFsIO::rename(const encfs::Path & pathSrc,
                              const encfs::Path & pathDst) {
  auto converted_src_path = _convertPath(pathSrc);
  auto converted_dst_path = _convertPath(pathDst);

  auto attrs_of_dst_parent = encfs::get_attrs(_base, converted_dst_path.dirname());
  auto attrs_of_src_parent = encfs::get_attrs(_base, converted_src_path.dirname());

  // if directories are the same and
  // the basenames are the same, just different case
  // then keep the case of the destination (this is an explicit recasing)
  if (attrs_of_dst_parent.file_id == attrs_of_src_parent.file_id &&
      attrs_of_dst_parent.volume_id == attrs_of_src_parent.volume_id &&
      converted_dst_path.basename() == converted_src_path.basename()) {
    return _base->rename(converted_src_path,
                         converted_dst_path.dirname().join(pathDst.basename()));
  }

  return _base->rename(converted_src_path, converted_dst_path);
}

void
UnicodeWrapperFsIO::unlink(const encfs::Path & path) {
  return _base->unlink(_convertPath(path));
}

void
UnicodeWrapperFsIO::rmdir(const encfs::Path & path) {
  return _base->rmdir(_convertPath(path));
}

encfs::FsFileAttrs
UnicodeWrapperFsIO::get_attrs(const encfs::Path & path) const {
  return _base->get_attrs(_convertPath(path));
}

void
UnicodeWrapperFsIO::set_times(const encfs::Path & path,
                              const opt::optional<encfs::fs_time_t> & atime,
                              const opt::optional<encfs::fs_time_t> & mtime) {
  return _base->set_times(_convertPath(path),
                          atime, mtime);
}

}

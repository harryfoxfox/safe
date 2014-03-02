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

#ifndef __Safe__RecentlyUsedBookmarkStoreV1__
#define __Safe__RecentlyUsedBookmarkStoreV1__

#include <safe/recent_paths_storage.hpp>

#include <encfs/fs/FsIO.h>

#include <memory>

#include <Cocoa/Cocoa.h>

namespace safe { namespace mac {

class PathResolver {
  std::shared_ptr<encfs::FsIO> _fs;
  RecentlyUsedByteStringStoreV1::ByteString _bs;

public:
  PathResolver(std::shared_ptr<encfs::FsIO> _fs,
               RecentlyUsedByteStringStoreV1::ByteString bs);

  std::pair<encfs::Path, bool>
  resolve_path() const;

  encfs::Path
  get_last_known_path() const;
};

class PathSerializer {
  std::shared_ptr<encfs::FsIO> _fs;

public:
  PathSerializer(std::shared_ptr<encfs::FsIO> fs)
  : _fs(std::move(fs)) {}

  RecentlyUsedByteStringStoreV1::ByteString
  serialize(encfs::Path) const;

  PathResolver
  deserialize(const RecentlyUsedByteStringStoreV1::ByteString & bs) const;
};

class RecentlyUsedNSURLStoreV1 : public RecentlyUsedPathStore<PathSerializer> {
  public:
    RecentlyUsedNSURLStoreV1(std::shared_ptr<encfs::FsIO> fs,
                             encfs::Path storage_file,
                             RecentlyUsedByteStringStoreV1::max_ent_t num_paths)
    : RecentlyUsedPathStore(fs,
                             std::move(storage_file),
                             "RecentlyUsedNSURLStoreV1",
                             num_paths,
                             PathSerializer(fs))
  {}
};

}}

#endif /* defined(__Safe__RecentlyUsedBookmarkStoreV1__) */

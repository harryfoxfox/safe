/*
 Safe: Encrypted File System
 Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

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

#import <safe/mac/RecentlyUsedNSURLStoreV1.hpp>

#import <safe/recent_paths_storage.hpp>
#import <safe/mac/util.hpp>

#import <Cocoa/Cocoa.h>

namespace safe { namespace mac {

RecentlyUsedByteStringStoreV1::ByteString
PathSerializer::serialize(encfs::Path path) const {
  NSURL *url = [NSURL fileURLWithPath:safe::mac::to_ns_string(((const std::string &)path).c_str())];

  NSError *err;
  NSData *data = [url bookmarkDataWithOptions:0
               includingResourceValuesForKeys:@[NSURLNameKey, NSURLPathKey]
                                relativeToURL:nil
                                        error:&err];
  if (!data) throw std::runtime_error("couldn't create bookmark: " + from_ns_string(err.localizedDescription));

  auto c_data = (RecentlyUsedByteStringStoreV1::byte_t *) data.bytes;
  return RecentlyUsedByteStringStoreV1::ByteString(c_data, c_data + data.length);
}

PathResolver
PathSerializer::deserialize(const RecentlyUsedByteStringStoreV1::ByteString & bs) const {
  return PathResolver(_fs, bs);
}

PathResolver::PathResolver(std::shared_ptr<encfs::FsIO> fs,
                           RecentlyUsedByteStringStoreV1::ByteString bs)
  : _fs(std::move(fs))
  , _bs(std::move(bs)) {
}

std::pair<encfs::Path, bool>
PathResolver::resolve_path() const {
  NSData *data_ref = [NSData dataWithBytesNoCopy:(void *)_bs.data() length:_bs.size() freeWhenDone:NO];

  NSError *err;
  BOOL data_is_stale;
  NSURL *url = [NSURL URLByResolvingBookmarkData:data_ref
                                         options:0
                                   relativeToURL:nil
                             bookmarkDataIsStale:&data_is_stale
                                           error:&err];
  if (!url) throw std::runtime_error("couldn't resolve bookmark: " + from_ns_string(err.localizedDescription));

  return std::make_pair(url_to_path(_fs, url), data_is_stale);
}

encfs::Path
PathResolver::get_last_known_path() const {
  NSData *data_ref = [NSData dataWithBytesNoCopy:(void *)_bs.data() length:_bs.size() freeWhenDone:NO];

  NSDictionary *resources = [NSURL resourceValuesForKeys:@[NSURLPathKey] fromBookmarkData:data_ref];
  if (!resources) throw std::runtime_error("name wasn't found!");
  NSString *ret = resources[NSURLPathKey];
  if (!ret) throw std::runtime_error("name wasn't found!");
  return string_to_path(_fs, ret);
}

}}


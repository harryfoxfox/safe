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

#import <safe/mac/unicode_fs.hpp>

#import <Foundation/Foundation.h>

namespace safe { namespace unicode_fs { namespace mac {

// following ARC-guidelines, this method should be have the
// substring "create" or "copy" in its name
static
NSString *
create_utf8_nsstring_from_str(const std::string & comp) {
  NSData *data = [NSData dataWithBytesNoCopy:(void *)comp.data()
                                      length:comp.size()
                                freeWhenDone:NO];
  return [NSString.alloc initWithData:data
                             encoding:NSUTF8StringEncoding];
}

std::string
normalize_path_component_for_fs(const std::string & comp) {
  @autoreleasepool {
    NSString *str = create_utf8_nsstring_from_str(comp);
    if (!str) throw std::runtime_error("invalid utf8");
    NSString *nfc_str = str.precomposedStringWithCanonicalMapping;
    return std::string(nfc_str.UTF8String);
  }
}

std::string
normalize_path_component_for_user(const std::string & comp) {
  @autoreleasepool {
    NSString *str = create_utf8_nsstring_from_str(comp);
    if (!str) throw std::runtime_error("invalid utf8");
    NSString *nfc_str = str.decomposedStringWithCanonicalMapping;
    return std::string(nfc_str.UTF8String);
  }
}

bool
is_normalized_path_component(const std::string & comp) {
  @autoreleasepool {
    NSString *str = create_utf8_nsstring_from_str(comp);
    if (!str) return false;
    return [str isEqualToString:str.precomposedStringWithCanonicalMapping];
  }
}

bool
normalized_path_components_equal(const std::string & comp_a,
                                 const std::string & comp_b) {
  @autoreleasepool {
    assert(is_normalized_path_component(comp_a));
    assert(is_normalized_path_component(comp_b));
    NSString *str_a = create_utf8_nsstring_from_str(comp_a);
    NSString *str_b = create_utf8_nsstring_from_str(comp_b);
    return [str_a compare:str_b
                  options:(NSCaseInsensitiveSearch | NSLiteralSearch)] == NSOrderedSame;
  }
}

}}}

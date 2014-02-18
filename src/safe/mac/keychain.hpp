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

#ifndef __Safe__mac_keychain__
#define __Safe__mac_keychain__

#include <safe/deferred.hpp>

#include <system_error>

#include <Security/Security.h>

namespace safe { namespace mac {

class sec_error : public std::system_error {
    char _what[128];
    
public:
    sec_error(OSStatus err)
    : std::system_error((int) err, std::generic_category()) {
        auto string_ref = SecCopyErrorMessageString(err, nullptr);
        auto _free_string_ref = safe::create_deferred(CFRelease, string_ref);
        strncpy(_what, ((__bridge NSString *) string_ref).UTF8String, sizeof(_what));
    }
    
    const char *
    what() const noexcept {
        return _what;
    }
};
  
NSString *
get_saved_password_for_location(NSURL *url);
    
void
save_password_for_location(NSURL *url, NSString *password);
    
}}

#endif /* defined(__Safe__mac_keychain__) */

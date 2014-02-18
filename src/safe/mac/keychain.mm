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

#include <safe/logging.h>
#include <safe/mac/keychain.hpp>

#include <memory>

#include <Cocoa/Cocoa.h>
#include <Security/Security.h>

namespace safe { namespace mac {

const char *SERVICE_NAME = "Safe";

static
SecKeychainItemRef
copy_keychain_item_for_location(NSURL *url) {
    const char *account_name = url.path.fileSystemRepresentation;
    
    SecKeychainItemRef item;
    auto ret = SecKeychainFindGenericPassword(nullptr,
                                              strlen(SERVICE_NAME),
                                              SERVICE_NAME,
                                              strlen(account_name),
                                              account_name,
                                              0,
                                              nullptr,
                                              &item);
    if (ret != noErr) throw sec_error(ret);
    
    return item;
}

NSString *
get_saved_password_for_location(NSURL *url) {
    auto keychain_item = copy_keychain_item_for_location(url);
    auto _free_keychain_item = safe::create_deferred(CFRelease, keychain_item);

    UInt32 password_length;
    void *password;
    
    auto ret = SecKeychainItemCopyContent(keychain_item,
                                          nullptr,
                                          nullptr,
                                          &password_length,
                                          &password);
    if (ret != noErr) throw sec_error(ret);
    auto _free_password = safe::create_deferred(SecKeychainItemFreeContent, nullptr, password);
    
    auto password_copy = std::unique_ptr<char[]>(new char[password_length + 1]);
    memcpy(password_copy.get(), password, password_length);
    password_copy[password_length] = '\0';
    return [NSString stringWithUTF8String:password_copy.get()];
}

void
save_password_for_location(NSURL *url, NSString *password) {
    const char *account_name = url.path.fileSystemRepresentation;
    const char *password_utf8 = password.UTF8String;
    
    SecKeychainItemRef keychain_item = nullptr;
    try {
        keychain_item = copy_keychain_item_for_location(url);
    }
    catch (const sec_error & err) {
        if (err.code().value() != errSecItemNotFound) throw;
    }
    
    if (keychain_item) {
        lbx_log_debug("modifying existing keychain entry");
        auto _free_keychain_item = safe::create_deferred(CFRelease, keychain_item);
        // just update the item with the new password
        auto ret = SecKeychainItemModifyContent(keychain_item,
                                                nullptr,
                                                strlen(password_utf8),
                                                password_utf8);
        if (ret != noErr) throw sec_error(ret);
    }
    else {
        lbx_log_debug("adding new password");
        auto ret = SecKeychainAddGenericPassword(nullptr,
                                                 strlen(SERVICE_NAME),
                                                 SERVICE_NAME,
                                                 strlen(account_name),
                                                 account_name,
                                                 strlen(password_utf8),
                                                 password_utf8,
                                                 nullptr);
        if (ret != noErr) throw sec_error(ret);
    }
}

}}

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
#include <safe/mac/util.hpp>

#include <memory>

#include <Cocoa/Cocoa.h>
#include <Security/Security.h>
#include <CommonCrypto/CommonDigest.h>

namespace safe { namespace mac {

const char *SERVICE_NAME = "Safe";
const char *KEYCHAIN_ID_FILE = ".safe_metadata.json";

std::runtime_error
nserror_to_exception(std::string context, NSError *err) {
    return std::runtime_error(context + ": " +
                              safe::mac::from_ns_string(err.localizedDescription));
}

static
NSDictionary *
write_directory_metadata(NSURL *dir_url, NSDictionary *dict) {
    NSURL *url = [dir_url URLByAppendingPathComponent:safe::mac::to_ns_string(KEYCHAIN_ID_FILE)];

    NSOutputStream *ostream = [NSOutputStream outputStreamWithURL:url append:NO];
    [ostream open];
    if (ostream.streamError) {
        throw nserror_to_exception("opening metadata file for write", ostream.streamError);
    }

    NSError *err = nil;
    auto ret = [NSJSONSerialization writeJSONObject:dict
                                           toStream:ostream
                                            options:0
                                              error:&err];
    if (!ret) {
        throw nserror_to_exception("writing json to metadata file", err);
    }

    return dict;
}

static
NSDictionary *
read_directory_metadata(NSURL *url, bool return_nil_if_doesnt_exist) {
    auto no_metadata_exists = [&] {
        return return_nil_if_doesnt_exist ? nil : write_directory_metadata(url, @{});
    };

    NSURL *id_path = [url URLByAppendingPathComponent:safe::mac::to_ns_string(KEYCHAIN_ID_FILE)];

    NSInputStream *stream = [NSInputStream inputStreamWithURL:id_path];
    [stream open];

    NSError *stream_error = [stream streamError];
    if (stream_error) {
        if ([NSPOSIXErrorDomain isEqualToString:[stream_error domain]] &&
            [stream_error code] == ENOENT) {
            // file didn't exist, create it
            lbx_log_debug("metadata file didn't exist, initializing");
            return no_metadata_exists();
        }
        else {
            throw nserror_to_exception("opening metadata for read", stream_error);
        }
    }

    NSError *err = nil;
    id metadata_typeless = [NSJSONSerialization JSONObjectWithStream:stream
                                                             options:0
                                                               error:&err];
    if (err) {
        // file was invalid format, reinitialize
        lbx_log_debug("metadata was not valid json, reinitializing");
        return no_metadata_exists();
    }

    if (![metadata_typeless isKindOfClass:NSDictionary.class]) {
        // file was invalid format, reinitialize
        lbx_log_debug("metadata was not a dictionary, reinitializing");
        return no_metadata_exists();
    }

    return metadata_typeless;
}

static
NSString *
base64enc(NSData *input) {
    SecTransformRef transform = SecEncodeTransformCreate(kSecBase64Encoding, nullptr);
    if (!transform) throw std::runtime_error("couldn't create transform encoder");
    auto _release_transform = safe::create_deferred(CFRelease, transform);

    auto ret = SecTransformSetAttribute(transform, kSecTransformInputAttributeName,
                                        (__bridge CFDataRef) input, nullptr);
    if (!ret) throw std::runtime_error("couldn't set transform attribute");

    auto ret2 = SecTransformExecute(transform, nullptr);
    if (!ret2) throw std::runtime_error("couldn't transform");
    auto _release_typeref = safe::create_deferred(CFRelease, ret2);

    NSData *d = [NSData dataWithData:(__bridge NSData *)ret2];

    NSString *toret = [NSString.alloc initWithData:d encoding:NSASCIIStringEncoding];
    // we should always be able to decode base64-encoded data into a character string format
    assert(toret);
    return toret;
}

static
NSData *
base64dec(NSString *input) {
    SecTransformRef transform = SecDecodeTransformCreate(kSecBase64Encoding, nullptr);
    if (!transform) throw std::runtime_error("couldn't create transform encoder");
    auto _release_transform = safe::create_deferred(CFRelease, transform);

    NSData *string_data = [input dataUsingEncoding:NSASCIIStringEncoding];
    if (!string_data) throw std::runtime_error("invalid string!");

    auto ret = SecTransformSetAttribute(transform, kSecTransformInputAttributeName,
                                        (__bridge CFDataRef) string_data, nullptr);
    if (!ret) throw std::runtime_error("couldn't set transform attribute");

    auto ret2 = SecTransformExecute(transform, nullptr);
    if (!ret2) throw std::runtime_error("couldn't transform");
    auto _release_typeref = safe::create_deferred(CFRelease, ret2);

    return [NSData dataWithData:(__bridge NSData *)ret2];
}

/*
 this function reads the keychain "account name" (id) from the metadata file
 in the specified directory. if the id is missing or is invalid, then this method will
 generate a new one.
 this function will throw an exception if it encounters and error while performing
 its actions.
 */
static
NSString *
read_keychain_id_for_path(NSURL *url, bool return_nil_if_doesnt_exist) {
    NSDictionary *metadata = read_directory_metadata(url, return_nil_if_doesnt_exist);
    if (!metadata) return nil;

    static const NSString *KEYCHAIN_ID_KEY = @"apple_keychain_unhashed_account_name";
    static const size_t KEYCHAIN_ID_RAW_LENGTH = CC_SHA256_DIGEST_LENGTH;

    NSString *base64_encoded_id = nil;
    id metadata_id = metadata[KEYCHAIN_ID_KEY];
    if ([metadata_id isKindOfClass:NSString.class]) {
        base64_encoded_id = metadata_id;
    }

    NSData *raw_id = nil;
    if (base64_encoded_id) {
        // first base64-decode the key
        try {
            raw_id = base64dec(base64_encoded_id);
        }
        catch (const std::exception & err) {
            lbx_log_debug("Error while base64-decoding string, creating new one: %s",
                          err.what());
        }
    }

    if (raw_id && raw_id.length != KEYCHAIN_ID_RAW_LENGTH) {
        raw_id = nil;
        lbx_log_debug("Key was wrong length, creating new one");
    }

    // signal to create new keychain id
    if (!raw_id) {
        if (return_nil_if_doesnt_exist) return nil;

        char random_buffer[KEYCHAIN_ID_RAW_LENGTH];
        arc4random_buf((void *) random_buffer, sizeof(random_buffer));

        // our ids are 256-bits
        raw_id = [NSData dataWithBytes:random_buffer length:sizeof(random_buffer)];

        // now base64 encode and store in database
        NSMutableDictionary *mutable_metadata = [NSMutableDictionary dictionaryWithDictionary:metadata];
        mutable_metadata[KEYCHAIN_ID_KEY] = base64enc(raw_id);
        write_directory_metadata(url, mutable_metadata);
    }

    // we cryptographically hash the account name to make it difficult
    // for an attacker to get Safe to access the keychain on their behalf
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    CC_SHA256_Update(&ctx, raw_id.bytes, raw_id.length);

    unsigned char hashed_buffer[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(hashed_buffer, &ctx);

    // finally convert the hashed data to base64 and return
    return base64enc([NSData dataWithBytesNoCopy:(void *)hashed_buffer
                                          length:CC_SHA256_DIGEST_LENGTH
                                    freeWhenDone:NO]);
}

static
SecKeychainItemRef
copy_keychain_item_for_account_name(NSString *account_name) {
    const char *account_name_utf8 = account_name.UTF8String;
    
    SecKeychainItemRef item;
    auto ret = SecKeychainFindGenericPassword(nullptr,
                                              strlen(SERVICE_NAME),
                                              SERVICE_NAME,
                                              strlen(account_name_utf8),
                                              account_name_utf8,
                                              0,
                                              nullptr,
                                              &item);
    if (ret != noErr) throw sec_error(ret);
    
    return item;
}

NSString *
get_saved_password_for_location(NSURL *url) {
    NSString *account_name = read_keychain_id_for_path(url, true);
    if (!account_name) return nil;

    auto keychain_item = copy_keychain_item_for_account_name(account_name);
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
    NSString *account_name = read_keychain_id_for_path(url, false);
    assert(account_name);

    SecKeychainItemRef keychain_item = nullptr;
    try {
        keychain_item = copy_keychain_item_for_account_name(account_name);
    }
    catch (const sec_error & err) {
        if (err.code().value() != errSecItemNotFound) throw;
    }

    const char *password_utf8 = password.UTF8String;
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
        const char *account_name_utf8 = account_name.UTF8String;
        auto ret = SecKeychainAddGenericPassword(nullptr,
                                                 strlen(SERVICE_NAME),
                                                 SERVICE_NAME,
                                                 strlen(account_name_utf8),
                                                 account_name_utf8,
                                                 strlen(password_utf8),
                                                 password_utf8,
                                                 nullptr);
        if (ret != noErr) throw sec_error(ret);
    }
}

}}

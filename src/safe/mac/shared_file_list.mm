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

#import <safe/mac/shared_file_list.hpp>

#import <safe/logging.h>
#import <safe/util.hpp>

namespace safe { namespace mac {

NSString *kSFXItemOwner = @"SFXItemOwner";
NSString *kSFXAnyOwner = @"SFXAnyOwner";

template<class F>
static
bool
iterate_shared_file_list(LSSharedFileListRef list_ref, F f) {
    // iterate through every item to find out if we're run at login
    UInt32 snapshot_seed;
    auto item_array_ref = LSSharedFileListCopySnapshot(list_ref, &snapshot_seed);
    if (!item_array_ref) throw std::runtime_error("couldn't create snapshot array");
    auto _release_item_array = safe::create_deferred(CFRelease, item_array_ref);

    auto array_size = CFArrayGetCount(item_array_ref);
    for (CFIndex i = 0; i < array_size; ++i) {
        auto item_ref = (LSSharedFileListItemRef) CFArrayGetValueAtIndex(item_array_ref, i);
        auto break_ = f(list_ref, item_ref);
        if (break_) return true;
    }
    return false;
}

template<class F>
static
bool
iterate_shared_file_list(CFStringRef list_type, F && f) {
    auto list_ref =
    LSSharedFileListCreate(nullptr, list_type, nullptr);
    if (!list_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, list_ref);

    return iterate_shared_file_list(list_ref, std::forward<F>(f));
}

static
bool
list_item_equals_nsurl(LSSharedFileListItemRef item_ref, NSURL *url) {
    CFURLRef out_url;
    auto err = LSSharedFileListItemResolve(item_ref,
                                           kLSSharedFileListNoUserInteraction,
                                           &out_url, nullptr);
    if (err != noErr) {
        // not sure under what circumstances this can happen but let's not fail on this
        lbx_log_error("Error while calling LSSharedFileListItemResolve: 0x%x", (unsigned) err);
        return false;
    }

    auto _free_url = safe::create_deferred(CFRelease, out_url);

    return [(__bridge NSURL *) out_url isEqual:url];
}

static
bool
list_item_owner_is(LSSharedFileListItemRef item_ref, NSString *owner) {
    if (owner == kSFXAnyOwner) return true;

    auto type_ref = LSSharedFileListItemCopyProperty(item_ref,
                                                     (__bridge CFStringRef) kSFXItemOwner);
    if (!type_ref) return !owner;
    auto _release_item = safe::create_deferred(CFRelease, type_ref);

    if (CFGetTypeID(type_ref) != CFStringGetTypeID()) return false;

    return [owner isEqualToString:(__bridge NSString *) type_ref];
}

void
add_url_with_owner_to_shared_file_list(CFStringRef list_type, NSURL *url, NSString *owner) {
    if (shared_file_list_contains_url_with_owner(list_type, url, owner)) return;

    auto list_ref =
    LSSharedFileListCreate(nullptr, list_type, nullptr);
    if (!list_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, list_ref);

    // create item
    auto new_item_ref = LSSharedFileListInsertItemURL(list_ref,
                                                      kLSSharedFileListItemBeforeFirst,
                                                      (__bridge CFStringRef) url.lastPathComponent,
                                                      nullptr,
                                                      (__bridge CFURLRef) url,
                                                      nullptr,
                                                      nullptr);
    if (!new_item_ref) throw std::runtime_error("item creation failed");
    auto _free_item_ref = safe::create_deferred(CFRelease, new_item_ref);

    // this makes the method "transactional" w.r.t. exceptions
    auto _remove_item = safe::create_deferred(LSSharedFileListItemRemove, list_ref, new_item_ref);

    if (owner) {
        auto status = LSSharedFileListItemSetProperty(new_item_ref,
                                                      (__bridge CFStringRef) kSFXItemOwner,
                                                      (__bridge CFTypeRef) owner);
        if (status != noErr) throw std::runtime_error("couldn't set property");
    }

    // operation was a success, don't delete item
    _remove_item.cancel();
}

bool
remove_url_with_owner_from_shared_file_list(CFStringRef list_type, NSURL *url, NSString *owner) {
    bool found = false;

    iterate_shared_file_list(list_type, [&](LSSharedFileListRef list_ref,
                                            LSSharedFileListItemRef item_ref) {
        if (list_item_owner_is(item_ref, owner) &&
            list_item_equals_nsurl(item_ref, url)) {
            auto ret = LSSharedFileListItemRemove(list_ref, item_ref);
            if (ret != noErr) throw std::runtime_error("error deleting item from list");
            found = true;
        }

        return false;
    });
    
    return found;
}

bool
shared_file_list_contains_url_with_owner(CFStringRef list_type, NSURL *url, NSString *owner) {
    return iterate_shared_file_list(list_type, [&](LSSharedFileListRef /*list_ref*/,
                                                   LSSharedFileListItemRef item_ref) {
        return (list_item_equals_nsurl(item_ref, url)  &&
                list_item_owner_is(item_ref, owner));
    });
}

bool
remove_items_with_owner_from_shared_file_list(CFStringRef list_type, NSString *owner) {
    bool found = false;

    iterate_shared_file_list(list_type, [&](LSSharedFileListRef list_ref,
                                            LSSharedFileListItemRef item_ref) {
        if (list_item_owner_is(item_ref, owner)) {
            auto ret = LSSharedFileListItemRemove(list_ref, item_ref);
            if (ret != noErr) throw std::runtime_error("error deleting item from list");
            found = true;
        }
        
        return false;
    });
    
    return found;
}

bool
shared_file_list_contains_live_item_with_owner(CFStringRef list_type, NSString *owner) {
    return iterate_shared_file_list(list_type, [&](LSSharedFileListRef /*list_ref*/,
                                                   LSSharedFileListItemRef item_ref) {
        if (!list_item_owner_is(item_ref, owner)) return false;

        CFURLRef out_url;
        auto err = LSSharedFileListItemResolve(item_ref,
                                               kLSSharedFileListNoUserInteraction,
                                               &out_url, nullptr);
        if (err != noErr) {
            // not sure under what circumstances this can happen but let's not fail on this
            lbx_log_error("Error while calling LSSharedFileListItemResolve: 0x%x", (unsigned) err);
            return false;
        }

        auto _free_url = safe::create_deferred(CFRelease, out_url);

        return (bool) [NSFileManager.defaultManager fileExistsAtPath:((__bridge NSURL *) out_url).path];
    });
}

}}

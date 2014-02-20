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

#import <safe/mac/shared_file_list.hpp>

#import <safe/logging.h>
#import <safe/util.hpp>

namespace safe { namespace mac {

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
        auto break_ = f(item_ref);
        if (break_) return true;
    }
    return false;
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

void
add_url_to_shared_file_list(CFStringRef list_type, NSURL *url) {
    // don't add a duplicate
    if (shared_file_list_contains_url(list_type, url)) return;

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
}

bool
remove_url_from_shared_file_list(CFStringRef list_type, NSURL *url) {
    auto list_ref =
    LSSharedFileListCreate(nullptr, list_type, nullptr);
    if (!list_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, list_ref);

    return iterate_shared_file_list(list_ref, [&](LSSharedFileListItemRef item_ref) {
        if (list_item_equals_nsurl(item_ref, url)) {
            auto ret = LSSharedFileListItemRemove(list_ref, item_ref);
            if (ret != noErr) throw std::runtime_error("error deleting item from list");
            return true;
        }

        return false;
    });
}

bool
shared_file_list_contains_url(CFStringRef list_type, NSURL *url) {
    auto list_ref =
    LSSharedFileListCreate(nullptr, list_type, nullptr);
    if (!list_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, list_ref);

    return iterate_shared_file_list(list_ref, [&](LSSharedFileListItemRef item_ref) {
        return list_item_equals_nsurl(item_ref, url);
    });
}

}}

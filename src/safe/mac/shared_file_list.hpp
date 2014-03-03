//
//  shared_file_list.h
//  Safe
//
//  Created by Rian Hunter on 2/19/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#ifndef __Safe__shared_file_list__
#define __Safe__shared_file_list__

#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>

namespace safe { namespace mac {

extern NSString *kSFXAnyOwner;

void
add_url_with_owner_to_shared_file_list(CFStringRef list_type, NSURL *url, NSString *owner);

bool
remove_url_with_owner_from_shared_file_list(CFStringRef list_type, NSURL *url, NSString *owner);

bool
shared_file_list_contains_url_with_owner(CFStringRef list_type, NSURL *url, NSString *owner);

bool
remove_items_with_owner_from_shared_file_list(CFStringRef list_type, NSString *owner);

bool
shared_file_list_contains_live_item_with_owner(CFStringRef list_type, NSString *owner);

}}

#endif /* defined(__Safe__shared_file_list__) */

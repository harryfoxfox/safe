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

void
add_url_to_shared_file_list(CFStringRef list_type, NSURL *url);

bool
remove_url_from_shared_file_list(CFStringRef list_type, NSURL *url);

bool
shared_file_list_contains_url(CFStringRef list_type, NSURL *url);

}}

#endif /* defined(__Safe__shared_file_list__) */

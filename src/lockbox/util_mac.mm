/*
 Lockbox: Encrypted File System
 Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>
 
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

#include <lockbox/util_mac.hpp>

namespace lockbox { namespace mac {

void
open_url(const char *url) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url]]];
}
    
void
initialize_window_for_dialog(NSWindow *window) {
    window.canHide = NO;
    window.level = NSModalPanelWindowLevel;
    [window center];
    [window makeKeyAndOrderFront:nil];
}
    
NSString *
to_ns_string(const char *a) {
    return [NSString.alloc initWithUTF8String:a];
}
    
}}


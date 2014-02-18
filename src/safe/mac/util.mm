/*
 Safe: Encrypted File System
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

#include <safe/mac/util.hpp>

#include <stdexcept>
#include <string>

#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <CoreServices/CoreServices.h>

namespace safe { namespace mac {

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
   
NSString *
to_ns_string(const std::string & a) {
    return to_ns_string(a.c_str());
}
    
static
OSStatus
SendAppleEventToSystemProcess(AEEventID EventToSend) {
    AEAddressDesc targetDesc;
    static const ProcessSerialNumber kPSNOfSystemProcess = { 0, kSystemProcess };
    AppleEvent eventReply = {typeNull, NULL};
    AppleEvent appleEventToSend = {typeNull, NULL};
        
    OSStatus error = noErr;
        
    error = AECreateDesc(typeProcessSerialNumber, &kPSNOfSystemProcess,
                         sizeof(kPSNOfSystemProcess), &targetDesc);
        
    if (error != noErr)
    {
        return(error);
    }
        
    error = AECreateAppleEvent(kCoreEventClass, EventToSend, &targetDesc,
                               kAutoGenerateReturnID, kAnyTransactionID, &appleEventToSend);
        
    AEDisposeDesc(&targetDesc);
    if (error != noErr)
    {
        return(error);
    }
        
    error = AESend(&appleEventToSend, &eventReply, kAENoReply,
                   kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
        
    AEDisposeDesc(&appleEventToSend);
    if (error != noErr)
    {
        return(error);
    }
        
    AEDisposeDesc(&eventReply);
        
    return(error);
}

void
reboot_machine() {
    auto status = SendAppleEventToSystemProcess(kAERestart);
    if (status != noErr) throw std::runtime_error("Sending system process reboot message failed!");
}

    
}}


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

#import <safe/mac/util.hpp>

#import <safe/constants.h>
#import <safe/util.hpp>

#import <stdexcept>
#import <string>

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <CoreServices/CoreServices.h>

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

std::string
from_ns_string(const NSString *a) {
  return std::string(a.UTF8String);
}

const char *
exception_location_to_string(ExceptionLocation el) {
#define _CV(e) case e: return #e
    switch (el) {
            _CV(ExceptionLocation::SYSTEM_CHANGES);
            _CV(ExceptionLocation::STARTUP);
            _CV(ExceptionLocation::MOUNT);
            _CV(ExceptionLocation::CREATE);
        default: /* notreached */ assert(false); return "";
    }
#undef _CV
}

void
report_exception(ExceptionLocation el, std::exception_ptr eptr) {
    std::string what;
    try {
        std::rethrow_exception(eptr);
    }
    catch (const std::exception & err) {
        what = err.what();
    }

    auto string_ref =
    CFURLCreateStringByAddingPercentEscapes(nullptr,
                                            (__bridge CFStringRef) safe::mac::to_ns_string(what),
                                            nullptr,
                                            (__bridge CFStringRef) @";/?:@&=+$,",
                                            kCFStringEncodingUTF8);
    auto _free_string_ref = safe::create_deferred(CFRelease, string_ref);

    auto url = (std::string(SAFE_REPORT_EXCEPTION_WEBSITE) + "?" +
                "where=" + exception_location_to_string(el) + "&" +
                "what=" + safe::mac::from_ns_string((__bridge NSString *) string_ref));
    open_url(url.c_str());
}

}}


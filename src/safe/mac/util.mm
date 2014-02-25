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

#import <sstream>
#import <stdexcept>
#import <string>

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <CoreServices/CoreServices.h>

namespace safe { namespace mac {

void
open_url(const std::string & url) {
  // just responsible for getting a raw string into
  // a browser address bar
  auto success = [NSWorkspace.sharedWorkspace
                  openURL:[NSURL URLWithString:safe::mac::to_ns_string(url)]];
  if (!success) throw std::runtime_error("failed to open url");
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

std::string
get_parseable_platform_version() {
    SInt32 major_version;
    SInt32 minor_version;
    SInt32 bug_fix_version;
    Gestalt(gestaltSystemVersionMajor, &major_version);
    Gestalt(gestaltSystemVersionMinor, &minor_version);
    Gestalt(gestaltSystemVersionBugFix, &bug_fix_version);

    std::ostringstream os;
    os << "macosx-" << major_version << "." << minor_version << "." << bug_fix_version;
    return os.str();
}

encfs::Path
string_to_path(std::shared_ptr<encfs::FsIO> fs, NSString *string) {
    auto path_string = std::string(string.fileSystemRepresentation);

    // strip trailing slash
    if (path_string[path_string.length() - 1] == '/') {
        path_string = path_string.substr(0, path_string.length() - 1);
    }

    return fs->pathFromString(path_string);
}

encfs::Path
url_to_path(std::shared_ptr<encfs::FsIO> fs, NSURL *url) {
    if (![url isFileURL]) throw std::runtime_error("Not file path: " + from_ns_string(url.description));
    return string_to_path(fs, url.filePathURL.path);
}

}}


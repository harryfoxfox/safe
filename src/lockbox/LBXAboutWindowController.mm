//
//  LBXAboutWindowController.m
//  Lockbox
//
//  Created by Rian Hunter on 1/4/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXAboutWindowController.h>

#import <lockbox/constants.h>
#import <lockbox/util_mac.hpp>

@implementation LBXAboutWindowController

- (void)windowDidLoad {
    [super windowDidLoad];
    lockbox::mac::initialize_window_for_dialog(self.window);
}

- (IBAction)getSourceCode:(id)sender {
    (void) sender;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:LOCKBOX_SOURCE_CODE_WEBSITE]]];
}
@end

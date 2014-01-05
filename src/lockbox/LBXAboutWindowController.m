//
//  LBXAboutWindowController.m
//  Lockbox
//
//  Created by Rian Hunter on 1/4/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXAboutWindowController.h>

#import <lockbox/constants.h>

@implementation LBXAboutWindowController

- (void)windowDidLoad {
    [super windowDidLoad];
    self.window.canHide = NO;
    self.window.level = NSModalPanelWindowLevel;
    [self.window center];
}

- (IBAction)getSourceCode:(id)sender {
    (void) sender;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:LOCKBOX_SOURCE_CODE_WEBSITE]]];
}
@end

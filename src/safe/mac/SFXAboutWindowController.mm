//
//  SFXAboutWindowController.m
//  Safe
//
//  Created by Rian Hunter on 1/4/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXAboutWindowController.h>

#import <safe/constants.h>
#import <safe/mac/util.hpp>

@implementation SFXAboutWindowController

- (void)windowDidLoad {
    [super windowDidLoad];
    safe::mac::initialize_window_for_dialog(self.window);
}

- (IBAction)getSourceCode:(id)sender {
    (void) sender;
    safe::mac::open_url(SAFE_SOURCE_CODE_WEBSITE);
}

@end

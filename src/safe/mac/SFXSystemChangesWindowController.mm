//
//  SFXSystemChangesWindowController.m
//  Safe
//
//  Created by Rian Hunter on 4/27/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXSystemChangesWindowController.h>

#import <safe/mac/util.hpp>


@interface SFXSystemChangesWindowController ()

@end

@implementation SFXSystemChangesWindowController

- (id)initWithBlock:(SystemChangesWindowBlock)block
{
    self = [self initWithWindowNibName:@"SFXSystemChangesWindowController"];
    if (self) {
        self.block = block;
    }
    return self;
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    safe::mac::initialize_window_for_dialog(self.window);
}

- (BOOL)windowShouldClose:(id)sender {
    (void) sender;
    self.block(self, SYSTEM_CHANGES_ACTION_NONE);
    // this effectively turns the window close button into a normal button
    // only the handler can close the window
    return false;
}

- (IBAction)moreInfoPressed:(id)sender {
    (void)sender;
    self.block(self, SYSTEM_CHANGES_ACTION_MORE_INFO);
}

- (IBAction)dontMakeChangesPressed:(id)sender {
    (void)sender;
    self.block(self, SYSTEM_CHANGES_ACTION_DONT_MAKE_CHANGES);
}

- (IBAction)makeChangesPressed:(id)sender {
    (void)sender;
    self.block(self, SYSTEM_CHANGES_ACTION_MAKE_CHANGES);
}
@end

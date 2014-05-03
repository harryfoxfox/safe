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

- (id)initWithBlock:(SystemChangesWindowBlock)block andMessage:(NSString *)msg
{
    self = [self initWithWindowNibName:@"SFXSystemChangesWindowController"];
    if (self) {
        self.block = block;
        self.message = msg;
    }
    return self;
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    self.messageTextField.stringValue = self.message;
    safe::mac::initialize_window_for_dialog(self.window);
}

- (void)_dispatchAction:(system_changes_action_t)action {
    self.block(self, action, self.suppressMessageCheckbox.state == NSOnState);
}

- (IBAction)moreInfoPressed:(id)sender {
    (void)sender;
    [self _dispatchAction:SYSTEM_CHANGES_ACTION_MORE_INFO];
}

- (IBAction)dontMakeChangesPressed:(id)sender {
    (void)sender;
    [self _dispatchAction:SYSTEM_CHANGES_ACTION_DONT_MAKE_CHANGES];
}

- (IBAction)makeChangesPressed:(id)sender {
    (void)sender;
    [self _dispatchAction:SYSTEM_CHANGES_ACTION_MAKE_CHANGES];
}
@end

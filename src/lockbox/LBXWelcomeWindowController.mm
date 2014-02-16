//
//  LBXWelcomeWindowController.m
//  Lockbox
//
//  Created by Rian Hunter on 1/3/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXWelcomeWindowController.h>

#import <lockbox/util_mac.hpp>

@interface LBXWelcomeWindowController () {
    welcome_window_action_t action;
}

@end

@implementation LBXWelcomeWindowController

- (id)initWithDelegate:(NSObject <LBXWelcomeWindowControllerDelegate> *)delegate {
    self = [self initWithWindowNibName:@"LBXWelcomeWindowController"];
    if (self) {
        self.delegate = delegate;
        self->action = WELCOME_WINDOW_NONE;
    }
    return self;
}

- (IBAction)createNewLockbox:(id)sender {
    (void)sender;
    self->action = WELCOME_WINDOW_CREATE;
    [self.window performClose:self];
}

- (IBAction)mountExistingLockbox:(id)sender {
    (void)sender;
    self->action = WELCOME_WINDOW_MOUNT;
    [self.window performClose:self];
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    lockbox::mac::initialize_window_for_dialog(self.window);
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
    [self.delegate welcomeWindowDone:self withAction:self->action];
}

@end

//
//  LBXWelcomeWindowController.m
//  Lockbox
//
//  Created by Rian Hunter on 1/3/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import "LBXWelcomeWindowController.h"

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
        self.window.delegate = self;
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
    
    self.window.canHide = NO;
    [self.window center];
    [self.window makeKeyAndOrderFront:self];
    self.window.level = NSModalPanelWindowLevel;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
    [self.delegate welcomeWindowDone:self withAction:self->action];
}

@end

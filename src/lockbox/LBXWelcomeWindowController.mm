//
//  LBXWelcomeWindowController.m
//  Lockbox
//
//  Created by Rian Hunter on 1/3/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXWelcomeWindowController.h>

#import <lockbox/util_mac.hpp>

@implementation LBXWelcomeWindowController

- (id)initWithDelegate:(NSObject <LBXWelcomeWindowControllerDelegate> *)delegate {
    decltype(delegate) __weak weakDelegate = delegate;
    WelcomeWindowDoneBlock b = ^(LBXWelcomeWindowController *sender, welcome_window_action_t action_) {
        [weakDelegate welcomeWindowDone:sender withAction:action_];
        return true;
    };
    return [self initWithBlock:b
                            title:nil
                          message:nil
                     button0Title:nil
                     button1Title:nil];
}

- (id)initWithBlock:(WelcomeWindowDoneBlock)block
               title:(NSString *)title
             message:(NSString *)message
        button0Title:(NSString *)button0Title
        button1Title:(NSString *)button1Title {
    self = [self initWithWindowNibName:@"LBXWelcomeWindowController"];
    if (self) {
        self.block = block;
        if (title) {
            self.window.title = title;
        }
        if (message) {
            self.messageTextField.stringValue = message;
        }
        if (button0Title) {
            self.button0.title = button0Title;
        }
        if (button1Title) {
            self.button1.title = button1Title;
        }
    }
    return self;
}

- (void)_dispatchAction:(welcome_window_action_t)action_ {
    auto should_close = self.block(self, action_);
    if (should_close) [self.window close];
}

- (IBAction)createNewLockbox:(id)sender {
    (void)sender;
    [self _dispatchAction:WELCOME_WINDOW_CREATE];
}

- (IBAction)mountExistingLockbox:(id)sender {
    (void)sender;
    [self _dispatchAction:WELCOME_WINDOW_MOUNT];
}

- (void)windowDidLoad {
    [super windowDidLoad];
    lockbox::mac::initialize_window_for_dialog(self.window);
}

- (BOOL)windowShouldClose:(id)sender {
    (void) sender;
    auto should_close = self.block(self, WELCOME_WINDOW_NONE);
    return should_close ? YES : NO;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
}

@end

//
//  SFXWelcomeWindowController.m
//  Safe
//
//  Created by Rian Hunter on 1/3/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXWelcomeWindowController.h>

#import <safe/mac/util.hpp>

@implementation SFXWelcomeWindowController

- (id)initWithDelegate:(NSObject <SFXWelcomeWindowControllerDelegate> *)delegate {
    decltype(delegate) __weak weakDelegate = delegate;
    WelcomeWindowDoneBlock b = ^(SFXWelcomeWindowController *sender, welcome_window_action_t action_) {
        [weakDelegate welcomeWindowDone:sender withAction:action_];
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
    self = [self initWithWindowNibName:@"SFXWelcomeWindowController"];
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
    self.block(self, action_);
}

- (IBAction)createNewSafe:(id)sender {
    (void)sender;
    [self _dispatchAction:WELCOME_WINDOW_CREATE];
}

- (IBAction)mountExistingSafe:(id)sender {
    (void)sender;
    [self _dispatchAction:WELCOME_WINDOW_MOUNT];
}

- (void)windowDidLoad {
    [super windowDidLoad];
    safe::mac::initialize_window_for_dialog(self.window);
}

- (BOOL)windowShouldClose:(id)sender {
    (void) sender;
    [self _dispatchAction:WELCOME_WINDOW_NONE];
    // this effectively turns the window close button into a normal button
    // only the handler can close the window
    return false;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
}

@end

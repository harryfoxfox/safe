//
//  LBXWelcomeWindowController.h
//  Lockbox
//
//  Created by Rian Hunter on 1/3/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

typedef enum {
    WELCOME_WINDOW_NONE,
    WELCOME_WINDOW_CREATE,
    WELCOME_WINDOW_MOUNT,
} welcome_window_action_t;

@class LBXWelcomeWindowController;

@protocol LBXWelcomeWindowControllerDelegate <NSObject>

- (void)welcomeWindowDone:(id)sender withAction:(welcome_window_action_t)action;

@end

@interface LBXWelcomeWindowController : NSWindowController <NSWindowDelegate>

@property (nonatomic, weak) NSObject <LBXWelcomeWindowControllerDelegate> *delegate;

- (id)initWithDelegate:(NSObject <LBXWelcomeWindowControllerDelegate> *)delegate;
- (IBAction)createNewLockbox:(id)sender;
- (IBAction)mountExistingLockbox:(id)sender;

@end

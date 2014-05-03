//
//  SFXSystemChangesWindowController.h
//  Safe
//
//  Created by Rian Hunter on 4/27/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

typedef enum {
    SYSTEM_CHANGES_ACTION_MORE_INFO,
    SYSTEM_CHANGES_ACTION_DONT_MAKE_CHANGES,
    SYSTEM_CHANGES_ACTION_MAKE_CHANGES,
} system_changes_action_t;

@class SFXSystemChangesWindowController;

typedef void (^SystemChangesWindowBlock)(SFXSystemChangesWindowController *, system_changes_action_t, BOOL dontShowAgain);

@interface SFXSystemChangesWindowController : NSWindowController

@property (nonatomic, strong) SystemChangesWindowBlock block;
@property (nonatomic, strong) NSString *message;
@property (weak) IBOutlet NSButton *makeChangesButton;
@property (weak) IBOutlet NSTextField *messageTextField;
@property (weak) IBOutlet NSButton *suppressMessageCheckbox;

- (id)initWithBlock:(SystemChangesWindowBlock)block andMessage:(NSString *)msg;

- (IBAction)moreInfoPressed:(id)sender;
- (IBAction)dontMakeChangesPressed:(id)sender;
- (IBAction)makeChangesPressed:(id)sender;

@end

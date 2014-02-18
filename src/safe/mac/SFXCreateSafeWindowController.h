//
//  SFXCreateSafeWindowController.h
//  Safe
//
//  Created by Rian Hunter on 11/6/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <safe/mac/mount.hpp>

#include <encfs/fs/FsIO.h>

#import <Cocoa/Cocoa.h>

@class SFXCreateSafeWindowController;

@protocol SFXCreateSafeWindowControllerDelegate <NSObject>

- (void)createSafeDone:(SFXCreateSafeWindowController *)wc
                    mount:(opt::optional<safe::mac::MountDetails>)a;

@end

@interface SFXCreateSafeWindowController : NSWindowController <NSWindowDelegate> {
    std::shared_ptr<encfs::FsIO> fs;
    opt::optional<safe::mac::MountDetails> maybeMount;
}

@property (nonatomic, weak) NSObject <SFXCreateSafeWindowControllerDelegate> *delegate;

@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSTextField *nameTextField;
@property (weak) IBOutlet NSSecureTextField *passwordSecureTextField;
@property (weak) IBOutlet NSSecureTextField *confirmSecureTextField;
@property (weak) IBOutlet NSButton *rememberPasswordCheckbox;

- (id)initWithDelegate:(NSObject <SFXCreateSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (IBAction)confirmCreate:(id)sender;
- (IBAction)cancelWindow:(id)sender;

@end

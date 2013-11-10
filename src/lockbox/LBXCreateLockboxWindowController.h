//
//  LBXCreateLockboxWindowController.h
//  Lockbox
//
//  Created by Rian Hunter on 11/6/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <lockbox/mount_mac.hpp>

#include <encfs/fs/FsIO.h>

#import <Cocoa/Cocoa.h>

@class LBXCreateLockboxWindowController;

@protocol LBXCreateLockboxWindowControllerDelegate <NSObject>

- (void)createLockboxCanceled:(LBXCreateLockboxWindowController *)wc;
- (void)createLockboxDone:(LBXCreateLockboxWindowController *)wc
                    mount:(lockbox::mac::MountDetails)a;

@end

@interface LBXCreateLockboxWindowController : NSWindowController <NSWindowDelegate> {
    std::shared_ptr<encfs::FsIO> fs;
}

@property (nonatomic, weak) NSObject <LBXCreateLockboxWindowControllerDelegate> *delegate;

@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSTextField *nameTextField;
@property (weak) IBOutlet NSSecureTextField *passwordSecureTextField;
@property (weak) IBOutlet NSSecureTextField *confirmSecureTextField;

- (id)initWithDelegate:(NSObject <LBXCreateLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (IBAction)confirmCreate:(id)sender;
- (IBAction)cancelWindow:(id)sender;

@end

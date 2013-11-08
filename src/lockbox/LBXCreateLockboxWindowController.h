//
//  LBXCreateLockboxWindowController.h
//  Lockbox
//
//  Created by Rian Hunter on 11/6/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <lockbox/mac_mount.hpp>

#include <encfs/fs/FsIO.h>

#import <Cocoa/Cocoa.h>

@protocol LBXCreateLockboxWindowControllerDelegate <NSObject>

- (void)userCanceled;
- (void)createDone:(const lockbox::mac::MountDetails &)a;

@end

@interface LBXCreateLockboxWindowController : NSWindowController

@property (nonatomic, weak) NSObject <LBXCreateLockboxWindowControllerDelegate> *delegate;

@property (assign) std::shared_ptr<encfs::FsIO> fs;

@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSTextField *nameTextField;
@property (weak) IBOutlet NSSecureTextField *passwordSecureTextField;
@property (weak) IBOutlet NSSecureTextField *confirmSecureTextField;

- (id)initWithDelegate:(NSObject <LBXCreateLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (IBAction)confirmCreate:(id)sender;
- (IBAction)cancelWindow:(id)sender;

@end

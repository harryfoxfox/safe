//
//  LBXMountLockboxWindowController.h
//  Lockbox
//
//  Created by Rian Hunter on 11/8/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <lockbox/mount_mac.hpp>

#import <Cocoa/Cocoa.h>

@class LBXMountLockboxWindowController;

@protocol LBXMountLockboxWindowControllerDelegate <NSObject>

- (void)startLockboxCanceled:(LBXMountLockboxWindowController *)wc;
- (void)startLockboxDone:(LBXMountLockboxWindowController *)wc
                    mount:(lockbox::mac::MountDetails)a;

@end

@interface LBXMountLockboxWindowController : NSWindowController <NSWindowDelegate> {
    std::shared_ptr<encfs::FsIO> fs;
}

@property (nonatomic, weak) NSObject <LBXMountLockboxWindowControllerDelegate> *delegate;

@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSSecureTextField *passwordSecureTextField;

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs
                  path:(encfs::Path)p;

- (IBAction)confirmStart:(id)sender;
- (IBAction)cancelStart:(id)sender;

@end

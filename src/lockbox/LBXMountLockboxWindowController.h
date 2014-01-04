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

- (void)mountLockboxDone:(LBXMountLockboxWindowController *)wc
                   mount:(opt::optional<lockbox::mac::MountDetails>)a;

- (opt::optional<lockbox::mac::MountDetails>)takeMount:(const encfs::Path &)p;

@end

@interface LBXMountLockboxWindowController : NSWindowController <NSWindowDelegate> {
    std::shared_ptr<encfs::FsIO> fs;
    opt::optional<lockbox::mac::MountDetails> maybeMount;
}

@property (nonatomic, weak) NSObject <LBXMountLockboxWindowControllerDelegate> *delegate;
@property (retain) NSURL *fileURL;

@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSSecureTextField *passwordSecureTextField;

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs
                  path:(opt::optional<encfs::Path>)maybePath;

- (IBAction)confirmStart:(id)sender;
- (IBAction)cancelStart:(id)sender;

@end

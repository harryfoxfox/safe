//
//  LBXStartLockboxWindowController.h
//  Lockbox
//
//  Created by Rian Hunter on 11/8/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <lockbox/mount_mac.hpp>

#import <Cocoa/Cocoa.h>

@class LBXStartLockboxWindowController;

@protocol LBXStartLockboxWindowControllerDelegate <NSObject>

- (void)startLockboxCanceled:(LBXStartLockboxWindowController *)wc;
- (void)startLockboxDone:(LBXStartLockboxWindowController *)wc
                    mount:(lockbox::mac::MountDetails)a;

@end


@interface LBXStartLockboxWindowController : NSWindowController {
    std::shared_ptr<encfs::FsIO> fs;
}

@property (nonatomic, weak) NSObject <LBXStartLockboxWindowControllerDelegate> *delegate;


@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSSecureTextField *passwordSecureTextField;

- (id)initWithDelegate:(NSObject <LBXStartLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (IBAction)confirmStart:(id)sender;
- (IBAction)cancelStart:(id)sender;

@end

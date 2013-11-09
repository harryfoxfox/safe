//
//  LBXAppDelegate.h
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXCreateLockboxWindowController.h>
#import <lockbox/LBXStartLockboxWindowController.h>

#import <lockbox/mac_mount.hpp>

#import <Cocoa/Cocoa.h>


@interface LBXAppDelegate : NSObject <NSApplicationDelegate, LBXCreateLockboxWindowControllerDelegate, LBXStartLockboxWindowControllerDelegate>
{
    std::vector<lockbox::mac::MountDetails> mounts;
    std::shared_ptr<encfs::FsIO> native_fs;
}

@property (retain) NSStatusItem *statusItem;
@property (retain) NSMutableArray *createWindows;
@property (retain) NSMutableArray *startWindows;

@end

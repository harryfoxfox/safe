//
//  LBXAppDelegate.h
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#import <lockbox/LBXCreateLockboxWindowController.h>

@interface LBXAppDelegate : NSObject <NSApplicationDelegate, LBXCreateLockboxWindowControllerDelegate>

@property (assign) std::shared_ptr<encfs::FsIO> native_fs;

@property (retain) LBXCreateLockboxWindowController *wc;

@end

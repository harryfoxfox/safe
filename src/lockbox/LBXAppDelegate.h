//
//  LBXAppDelegate.h
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXAboutWindowController.h>
#import <lockbox/LBXCreateLockboxWindowController.h>
#import <lockbox/LBXMountLockboxWindowController.h>
#import <lockbox/LBXWelcomeWindowController.h>

#import <lockbox/mount_mac.hpp>
#import <lockbox/recent_paths_storage.hpp>

#import <Cocoa/Cocoa.h>


@interface LBXAppDelegate : NSObject <NSApplicationDelegate, LBXCreateLockboxWindowControllerDelegate, LBXMountLockboxWindowControllerDelegate, NSWindowDelegate, NSUserNotificationCenterDelegate, NSMenuDelegate, LBXWelcomeWindowControllerDelegate>
{
    std::vector<lockbox::mac::MountDetails> mounts;
    std::shared_ptr<encfs::FsIO> native_fs;
    NSInteger lastModifierFlags;
    std::unique_ptr<lockbox::RecentlyUsedPathStoreV1> path_store;
}

@property (retain) NSStatusItem *statusItem;
@property (retain) NSMutableArray *createWindows;
@property (retain) NSMutableArray *mountWindows;
@property (retain) NSRunningApplication *lastActiveApp;
@property (retain) LBXWelcomeWindowController *welcomeWindowDelegate;
@property (retain) NSWindowController *aboutWindowController;
@property (retain) LBXWelcomeWindowController *systemChangesWindowController;


- (void)createNewLockbox:(id)sender;
- (void)mountExistingLockbox:(id)sender;
- (void)welcomeWindowDone:(id)sender withAction:(welcome_window_action_t)action;

@end

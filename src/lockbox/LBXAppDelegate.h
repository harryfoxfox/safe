//
//  LBXAppDelegate.h
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXCreateLockboxWindowController.h>
#import <lockbox/LBXMountLockboxWindowController.h>

#import <lockbox/mount_mac.hpp>
#import <lockbox/recent_paths_storage.hpp>

#import <Cocoa/Cocoa.h>


@interface LBXAppDelegate : NSObject <NSApplicationDelegate, LBXCreateLockboxWindowControllerDelegate, LBXMountLockboxWindowControllerDelegate, NSWindowDelegate, NSUserNotificationCenterDelegate, NSMenuDelegate>
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
// NSWindow cannot be a weak reference, have to use assign
@property (assign) IBOutlet NSWindow *aboutWindow;
@property (weak) IBOutlet NSTextField *aboutWindowText;

- (IBAction)aboutWindowOK:(NSButton *)sender;
- (IBAction)aboutWindowGetSourceCode:(NSButton *)sender;

@end

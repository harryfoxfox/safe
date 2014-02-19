//
//  SFXAppDelegate.h
//  Safe
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXAboutWindowController.h>
#import <safe/mac/SFXCreateSafeWindowController.h>
#import <safe/mac/SFXMountSafeWindowController.h>
#import <safe/mac/SFXWelcomeWindowController.h>

#import <safe/mac/mount.hpp>
#import <safe/mac/RecentlyUsedNSURLStoreV1.hpp>

#import <Cocoa/Cocoa.h>


@interface SFXAppDelegate : NSObject <NSApplicationDelegate, SFXCreateSafeWindowControllerDelegate, SFXMountSafeWindowControllerDelegate, NSWindowDelegate, NSUserNotificationCenterDelegate, NSMenuDelegate, SFXWelcomeWindowControllerDelegate>
{
    std::vector<safe::mac::MountDetails> mounts;
    std::shared_ptr<encfs::FsIO> native_fs;
    NSInteger lastModifierFlags;
    std::unique_ptr<safe::mac::RecentlyUsedNSURLStoreV1> path_store;
}

@property (retain) NSStatusItem *statusItem;
@property (retain) NSMutableArray *createWindows;
@property (retain) NSMutableArray *mountWindows;
@property (retain) NSRunningApplication *lastActiveApp;
@property (retain) SFXWelcomeWindowController *welcomeWindowDelegate;
@property (retain) NSWindowController *aboutWindowController;
@property (retain) SFXWelcomeWindowController *systemChangesWindowController;


- (void)createNewSafe:(id)sender;
- (void)mountExistingSafe:(id)sender;
- (void)welcomeWindowDone:(id)sender withAction:(welcome_window_action_t)action;

@end

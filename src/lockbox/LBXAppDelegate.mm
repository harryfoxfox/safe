//
//  LBXAppDelegate.mm
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXAppDelegate.h>

#import <lockbox/mac_mount.hpp>

#import <lockbox/lockbox_server.hpp>

#import <davfuse/logging.h>

enum {
    CHECK_MOUNT_INTERVAL_IN_SECONDS = 1,
};

@implementation LBXAppDelegate

- (void)_newMount:(lockbox::mac::MountDetails)md {
    self->mounts.push_back(std::move(md));
    [self _updateStatusMenu];
    self->mounts.back().open_mount();
}

- (void)createLockboxCanceled:(LBXCreateLockboxWindowController *)wc {
    [self.createWindows removeObject:wc];
}

- (void)createLockboxDone:(LBXCreateLockboxWindowController *)wc
                    mount:(lockbox::mac::MountDetails)md {
    [self.createWindows removeObject:wc];
    [self _newMount:std::move(md)];
}

- (void)startLockboxCanceled:(LBXStartLockboxWindowController *)wc {
    [self.startWindows removeObject:wc];
}

- (void)startLockboxDone:(LBXStartLockboxWindowController *)wc
                   mount:(lockbox::mac::MountDetails)md {
    [self.startWindows removeObject:wc];
    [self _newMount:std::move(md)];
}

- (void)openMount:(id)sender {
    NSMenuItem *mi = sender;
    
    NSInteger mount_idx = mi.tag;
    assert(mount_idx >= 0);
    
    if ((NSUInteger) mount_idx >= self->mounts.size()) {
        // this mount index is invalid now
    }
    
    self->mounts[mount_idx].open_mount();
}

- (void)startABitvault:(id)sender {
    (void)sender;
    LBXStartLockboxWindowController *wc = [[LBXStartLockboxWindowController alloc]
                                            initWithDelegate:self fs:self->native_fs];
    [self.startWindows addObject:wc];
}

- (void)createABitvault:(id)sender {
    (void)sender;
    LBXCreateLockboxWindowController *wc = [[LBXCreateLockboxWindowController alloc]
                                            initWithDelegate:self fs:self->native_fs];
    [self.createWindows addObject:wc];
}

- (void)getBitvaultSource:(id)sender {
    (void)sender;
}

- (void)testBubble:(id)sender {
    (void)sender;
}

- (void)quitBitvault:(id)sender {
    [NSApp terminate:sender];
}

- (NSMenuItem *)_addItemToMenu:(NSMenu *)menu
             title:(NSString *)title
              action:(SEL)sel {
    NSMenuItem *mi = [[NSMenuItem alloc] initWithTitle:title
                                                action:sel
                                         keyEquivalent:@""];
    mi.target = self;
    [menu addItem:mi];
    return mi;
}

- (void)_updateStatusMenu {
    NSMenu *menu = self.statusItem.menu;
    
    [menu removeAllItems];
    
    // Menu is:
    // [ (Open | Stop) <mount name> ]
    // ...
    // [ <separator> ]
    // Start an Existing Bitvault
    // Create a New Bitvault
    // Get Bitvault Source Code
    // [ Test Bubble ]
    // Quit Bitvault
    
    // TODO: detect when alt is pressed to change Open -> Stop
    NSInteger tag = 0;
    for (const auto & md : self->mounts) {
        NSString *title = [NSString stringWithFormat:@"Open %s",
                           md.get_mount_name().c_str(), nil];
        NSMenuItem *mi = [self _addItemToMenu:menu
                                        title:title
                                       action:@selector(openMount:)];
        static_assert(sizeof(&md) <= sizeof(NSInteger),
                      "NSInteger is not large enough to hold a pointer");
        [mi setTag:tag];
        ++tag;
    }

    if (!self->mounts.empty()) {
        [menu addItem:[NSMenuItem separatorItem]];
    }
  
    // Start an Existing Bitvault
    [self _addItemToMenu:menu
                   title:@"Start an Existing Bitvault"
                  action:@selector(startABitvault:)];
    
    // Create a New Bitvault
   [self _addItemToMenu:menu
                  title:@"Create a New Bitvault"
                 action:@selector(createABitvault:)];
    
    // Get Bitvault Source Code
    [self _addItemToMenu:menu
                   title:@"Get Bitvault Source Code"
                  action:@selector(getBitvaultSource:)];
    
#ifndef NDEBUG
    // Test Bubble
    [self _addItemToMenu:menu
                   title:@"Test Bubble"
                  action:@selector(testBubble:)];
#endif
    
    // Quit Bitvault
    [self _addItemToMenu:menu
                   title:@"Quit Bitvault"
                  action:@selector(quitBitvault:)];
}

- (void)_setupStatusBar {
    NSMenu *statusMenu = [[NSMenu alloc] init];
    
    NSStatusBar *sb = [NSStatusBar systemStatusBar];
    self.statusItem = [sb statusItemWithLength:NSVariableStatusItemLength];
    
    self.statusItem.title = @"Bitvault";
    self.statusItem.highlightMode = YES;
    self.statusItem.menu = statusMenu;
    
    [self _updateStatusMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    (void)aNotification;
    
    log_printer_default_init();
    logging_set_global_level(LOG_DEBUG);
    log_debug("Hello world!");
    
    lockbox::global_webdav_init();
    
    self->native_fs = lockbox::create_native_fs();
    self.createWindows = [NSMutableArray array];
    self.startWindows = [NSMutableArray array];

    [self _setupStatusBar];
    
    // kickoff mount watch timer
    [self timerFire:self];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    
    for (auto & md : self->mounts) {
        md.unmount();
    }
    
    lockbox::global_webdav_shutdown();
}

- (void)timerFire:(id)p {
    (void)p;

    for (auto it = self->mounts.begin(); it != self->mounts.end();) {
        if (!it->is_still_mounted()) {
            it->signal_stop();
            it = self->mounts.erase(it);
            [self _updateStatusMenu];
        }
        else ++it;
    }

    [self performSelector:@selector(timerFire:) withObject:nil afterDelay:CHECK_MOUNT_INTERVAL_IN_SECONDS];
}

@end

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

#import <stdio.h>

#import <dispatch/dispatch.h>

#import <libgen.h>
#import <pthread.h>

#import <sys/stat.h>

enum {
    CHECK_MOUNT_INTERVAL_IN_SECONDS = 1,
};

@implementation LBXAppDelegate

- (void)userCanceled {
}

- (void)createDone:(const lockbox::mac::MountDetails &)md {
    (void)md;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    (void)aNotification;
    
    log_printer_default_init();
    logging_set_global_level(LOG_DEBUG);
    log_debug("Hello world!");
    
    self.native_fs = lockbox::create_native_fs();
    
    self.wc = [[LBXCreateLockboxWindowController alloc]
               initWithDelegate:self
               fs:self.native_fs];
    
    [self.wc.window makeKeyAndOrderFront:self];
    [self.wc.window center];
    
    /*
    char *command;
    int ret_asprintf = asprintf(&command, "mount_webdav -S -v EncryptedFS http://localhost:8080/ \"%s\"",
                                mount_point);
     */
}

- (void)timerFire:(id)p {
    (void)p;
    struct stat parent_st;
    struct stat child_st;
    int ret_stat_1;
    int ret_stat_2;


    /* this is ghetto */
//    const char *child = self.dstPathControl.URL.path.UTF8String;
//    char *dup = strdup(self.dstPathControl.URL.path.UTF8String);
    char *child = NULL;
    char *parent = dirname(NULL);

    NSLog(@"CHILD: %s, PARENT: %s", child, parent);

    ret_stat_1 = stat(child, &child_st);
    if (ret_stat_1 < 0) {
        goto done;
    }

    ret_stat_2 = lstat(parent, &parent_st);
    if (ret_stat_2 < 0) {
        goto done;
    }

    if (parent_st.st_dev == child_st.st_dev) {
        [[NSApplication sharedApplication] terminate:self];
    }

done:
    [self performSelector:@selector(timerFire:) withObject:nil afterDelay:CHECK_MOUNT_INTERVAL_IN_SECONDS];
}

@end

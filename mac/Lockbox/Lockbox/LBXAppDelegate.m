//
//  LBXAppDelegate.m
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <sys/stat.h>

#include <libgen.h>
#include <pthread.h>

#include <dispatch/dispatch.h>

#include <stdio.h>

#include "run_encfs.h"

#import "LBXAppDelegate.h"

typedef struct {
    void *delegate;
    char *source;
    char *destination;
    char *password;
    run_encfs_error_t error_run_encfs;
    bool run_encfs_done;
    bool mounted;
} StartWebdavEncfsServerArgs;

const NSTimeInterval CHECK_MOUNT_INTERVAL_IN_SECONDS = 5.0;

static void *
mount_webdav_thread_fn(void *p) {
    StartWebdavEncfsServerArgs *args = p;
    char *mount_point = args->destination;

    NSLog(@"Mount point is %s", mount_point);

    /* try to run the mount_webdav program */
    char *command;
    int ret_asprintf = asprintf(&command, "mount_webdav -S -v EncryptedFS http://localhost:8080/ \"%s\"",
                                mount_point);
    if (ret_asprintf < 0) {
        abort();
    }

    time_t finish = time(NULL);
    if (finish < 0) {
        abort();
    }

    finish += 60 * 60;
    while (!args->mounted && !args->run_encfs_done) {
        time_t cur_time = time(NULL);
        if (cur_time >= finish) {
            break;
        }

        int ret = system(command);
        NSLog(@"RET FROM SYSTEM: %d <- \"%s\"", ret, command);
        args->mounted = !ret;
        sleep(1);
    }

    free(command);

    NSValue *n = [NSValue valueWithPointer:args];
    [(__bridge LBXAppDelegate *)args->delegate
     performSelectorOnMainThread:@selector(onMountDone:)
     withObject:n waitUntilDone:NO];

    return NULL;
}

static void *
run_webdav_server_thread_fn(void *p) {
    StartWebdavEncfsServerArgs *args = p;

    NSLog(@"MOUNTING");
    NSLog(@"Source: \"%s\"", args->source);
    NSLog(@"Destination: \"%s\"", args->destination);

    args->run_encfs_done = false;
    args->mounted = false;

    /* start another thread to mount file system */
    pthread_t mount_thread;
    int ret_pthread_create =
        pthread_create(&mount_thread, NULL, mount_webdav_thread_fn, args);
    if (ret_pthread_create < 0) {
        abort();
    }

    args->error_run_encfs =
        run_encfs(args->source, args->password);
    args->run_encfs_done = true;

    return NULL;
}

@implementation LBXAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    init_encfs();
    [self.window center];
    [self.srcPathControl setURL:[NSURL fileURLWithPath:NSHomeDirectory()]];
    [self.dstPathControl setURL:[NSURL fileURLWithPath:NSHomeDirectory()]];
}

- (void)timerFire:(id)p {
    /* this is ghetto */
    const char *child = self.dstPathControl.URL.path.UTF8String;
    char *dup = strdup(self.dstPathControl.URL.path.UTF8String);
    char *parent = dirname(dup);

    NSLog(@"CHILD: %s, PARENT: %s", child, parent);

    struct stat child_st;
    int ret_stat_1 = stat(child, &child_st);
    if (ret_stat_1 < 0) {
        goto done;
    }

    struct stat parent_st;
    int ret_stat_2 = lstat(parent, &parent_st);
    if (ret_stat_2 < 0) {
        goto done;
    }

    if (parent_st.st_dev == child_st.st_dev) {
        [[NSApplication sharedApplication] terminate:self];
    }

done:
    free(dup);
    [self performSelector:@selector(timerFire:) withObject:nil afterDelay:CHECK_MOUNT_INTERVAL_IN_SECONDS];
}

- (void)onMountDone:(id)p {
    StartWebdavEncfsServerArgs *args = [(NSValue *)p pointerValue];

    if (args->mounted) {
        char *sup;
        int ret = asprintf(&sup, "open \"%s\"", self.dstPathControl.URL.path.UTF8String);
        if (ret < 0) {
            abort();
        }

        [self.window close];

        system(sup);

        free(sup);

        /* okay now when the volume is ummounted kill the program */
        [self performSelector:@selector(timerFire:) withObject:nil afterDelay:CHECK_MOUNT_INTERVAL_IN_SECONDS];
    }
    else if (args->error_run_encfs) {
        NSLog(@"Server is done!");

        NSString *errorString = nil;
        switch (args->error_run_encfs) {
        case RUN_ENCFS_ERROR_IS_NOT_DIRECTORY:
            errorString = @"The specified encrypted directory is not actually a directory, please re-select an encrypted directory.";
            break;
        case RUN_ENCFS_ERROR_GENERAL:
            errorString = @"Couldn't mount the specified encrypted directory for an unknown reason. Try restarting the program.";
            break;
        case RUN_ENCFS_ERROR_PASSWORD_INCORRECT:
            errorString = @"The specified password was incorrect";
            break;
        default:
            /* this should never happen, so we crash hard */
            abort();
            break;
        }

        NSLog(@"Message: %@", errorString);

        [NSApp endSheet:self.sheetPanel];

        NSAlert *a = [[NSAlert alloc] init];
        [a setMessageText:@"Mount Error"];
        [a setInformativeText:errorString];
        [a beginSheetModalForWindow:self.window modalDelegate:nil didEndSelector:nil contextInfo:nil];
    }
    else {
        /* this should happen on normal shutdown but before mount_webdav was successful, abort() for now */
        abort();
    }
}

- (void)didEndSheet:(NSWindow *)sheet returnCode:(NSInteger)returnCode contextInfo:(void *)contextInfo {
    [sheet orderOut:self];
}

- (IBAction)mountEncryptedFS:(id)sender {
    /* TODO: check if input is valid */

    [self.mountProgressIndicator startAnimation:self];

    [NSApp beginSheet: self.sheetPanel
       modalForWindow: self.window
        modalDelegate: self
       didEndSelector: @selector(didEndSheet:returnCode:contextInfo:)
          contextInfo: nil];

    StartWebdavEncfsServerArgs *f = malloc(sizeof(*f));
    f->delegate = (__bridge void *)self;
    f->destination = strdup(self.dstPathControl.URL.path.UTF8String);
    f->source = strdup(self.srcPathControl.URL.path.UTF8String);
    f->password = strdup(self.passwordTextField.stringValue.UTF8String);

    pthread_t new_thread;
    int ret_pthread_create = pthread_create(&new_thread, NULL, run_webdav_server_thread_fn, f);
    if (ret_pthread_create < 0) {
        free(f);
        abort();
    }
}

@end

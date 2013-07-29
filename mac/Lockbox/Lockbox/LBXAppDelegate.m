//
//  LBXAppDelegate.m
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

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
} StartWebdavEncfsServerArgs;

static void *
run_mount_webdav_process_fn(void *p) {
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

    bool mounted = false;
    finish += 60 * 60;
    while (!mounted) {
        time_t cur_time = time(NULL);
        if (cur_time >= finish) {
            break;
        }

        int ret = system(command);
        NSLog(@"RET FROM SYSTEM: %d <- \"%s\"", ret, command);
        mounted = !ret;
    }

    [(__bridge LBXAppDelegate *)args->delegate performSelectorOnMainThread:@selector(onMountDone:) withObject:nil waitUntilDone:NO];
    
    return NULL;
}

static void *
mount_webdav_thread_fn(void *p) {
    StartWebdavEncfsServerArgs *args = p;
    
    NSLog(@"MOUNTING");
    NSLog(@"Source: \"%s\"", args->source);
    NSLog(@"Destination: \"%s\"", args->destination);
    
    pthread_t mount_thread;
    /* start another thread to mount file system */
    int ret_pthread_create =
        pthread_create(&mount_thread, NULL, run_mount_webdav_process_fn, args);
    if (ret_pthread_create < 0) {
        abort();
    }
    
    bool success_run_webdav_server =
        run_encfs(args->source, args->password);
    if (!success_run_webdav_server) {
        abort();
    }
    return NULL;
}

@implementation LBXAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    [self.srcPathControl setURL:[NSURL fileURLWithPath:NSHomeDirectory()]];
    [self.dstPathControl setURL:[NSURL fileURLWithPath:NSHomeDirectory()]];
}

- (void)awakeFromNib {
}

- (void)onMountDone:(id)args {
    char *sup;
    int ret = asprintf(&sup, "open \"%s\"", self.dstPathControl.URL.path.UTF8String);
    if (ret < 0) {
        abort();
    }

    [self.window close];

    system(sup);

    free(sup);
}

- (IBAction)mountEncryptedFS:(id)sender {
    /* TODO: check if input is valid */

    [self.mountProgressIndicator startAnimation:self];
    
    [NSApp beginSheet: self.sheetPanel
       modalForWindow: self.window
        modalDelegate: self
       didEndSelector: nil
          contextInfo: nil];
    
    StartWebdavEncfsServerArgs *f = malloc(sizeof(*f));
    f->delegate = (__bridge void *)self;
    f->destination = strdup(self.dstPathControl.URL.path.UTF8String);
    f->source = strdup(self.srcPathControl.URL.path.UTF8String);
    f->password = strdup(self.passwordTextField.stringValue.UTF8String);
    
    pthread_t new_thread;
    int ret_pthread_create = pthread_create(&new_thread, NULL, mount_webdav_thread_fn, f);
    if (ret_pthread_create < 0) {
        abort();
    }
}


@end

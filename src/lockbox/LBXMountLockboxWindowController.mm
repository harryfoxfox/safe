//
//  LBXMountLockboxWindowController.m
//  Lockbox
//
//  Created by Rian Hunter on 11/8/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXMountLockboxWindowController.h>

#import <lockbox/LBXProgressSheetController.h>
#import <lockbox/constants.h>
#import <lockbox/logging.h>
#import <lockbox/mount_lockbox_dialog_logic.hpp>

@interface LBXMountLockboxWindowController ()

@end

@implementation LBXMountLockboxWindowController

- (id)initWithWindow:(NSWindow *)window
{
    self = [super initWithWindow:window];
    if (self) {
        // Initialization code here.
    }
    return self;
}

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_
               fileURL:(NSURL *)fileURL {
    NSLog(@"sup start");
    
    self = [self initWithWindowNibName:@"LBXMountLockboxWindowController"];
    if (self) {
        self.delegate = del;
        self->fs = fs_;
        
        // load window
        (void) self.window;
        
        if (!fileURL) fileURL = [NSURL fileURLWithPath:NSHomeDirectory()];
        else {
            // move focus to password
            [self.window makeFirstResponder:self.passwordSecureTextField];
        }
        self.locationPathControl.URL = fileURL;
        
        self.window.canHide = NO;
        [self.window center];
        [self.window makeKeyAndOrderFront:self];
        self.window.level = NSModalPanelWindowLevel;
        self.window.delegate = self;
    }
    
    return self;
}

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_ {
    return [self initWithDelegate:del
                               fs:fs_
                          fileURL:nil];
    
}

- (id)initWithDelegate:(NSObject <LBXMountLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_
                  path:(encfs::Path)p {
    return [self initWithDelegate:del
                               fs:fs_
                          fileURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:p.c_str()]]];
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
    [self.delegate mountLockboxDone:self
                              mount:std::move(self->maybeMount)];
}

- (IBAction)confirmStart:(id)sender {
    (void) sender;
    
    encfs::SecureMem password([self.passwordSecureTextField.stringValue lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
    memcpy(password.data(),
           self.passwordSecureTextField.stringValue.UTF8String,
           password.size());
    
    auto error_msg =
        lockbox::verify_mount_lockbox_dialog_fields(self->fs,
                                                    self.locationPathControl.URL.path.UTF8String,
                                                    password);
    if (error_msg) {
        inputErrorAlert(self.window,
                        [NSString stringWithUTF8String:error_msg->title.c_str()],
                        [NSString stringWithUTF8String:error_msg->message.c_str()]);
        return;
    }
    
    auto encrypted_container_path = self->fs->pathFromString(self.locationPathControl.URL.path.fileSystemRepresentation);
    
    auto onFail = ^(const std::exception_ptr & eptr) {
        bool alerted = false;
        if (eptr != std::exception_ptr()) {
            try {
                std::rethrow_exception(eptr);
            }
            catch (const encfs::ConfigurationFileDoesNotExist & /*err*/) {
                inputErrorAlert(self.window,
                                [NSString stringWithUTF8String:LOCKBOX_DIALOG_NO_CONFIG_EXISTS_TITLE],
                                [NSString stringWithUTF8String:LOCKBOX_DIALOG_NO_CONFIG_EXISTS_MESSAGE]);
                alerted = true;
            }
            catch (const std::exception &err) {
                lbx_log_error("Error while mounting: %s", err.what());
            }
        }
        
        if (!alerted) {
            inputErrorAlert(self.window,
                            [NSString stringWithUTF8String:LOCKBOX_DIALOG_UNKNOWN_MOUNT_ERROR_TITLE],
                            [NSString stringWithUTF8String:LOCKBOX_DIALOG_UNKNOWN_MOUNT_ERROR_MESSAGE]);
        }
    };
    
    auto onMountSuccess = ^(lockbox::mac::MountDetails md) {
        self->maybeMount = std::move(md);
        [self.window performClose:self];
    };
    
    auto onVerifySuccess = ^(opt::optional<encfs::EncfsConfig> maybeConfig) {
        if (maybeConfig) {
            self->maybeMount = [self.delegate takeMount:encrypted_container_path];
            if (self->maybeMount) {
                [self.window performClose:self];
                return;
            }
            
            // success pass values to app
            showBlockingSheetMessage(self.window,
                                     [NSString stringWithUTF8String:LOCKBOX_PROGRESS_MOUNTING_EXISTING_TITLE],
                                     onMountSuccess,
                                     onFail,
                                     lockbox::mac::mount_new_encfs_drive,
                                     self->fs,
                                     encrypted_container_path, *maybeConfig, password);
        }
        else {
            inputErrorAlert(self.window,
                            [NSString stringWithUTF8String:LOCKBOX_DIALOG_PASS_INCORRECT_TITLE],
                            [NSString stringWithUTF8String:LOCKBOX_DIALOG_PASS_INCORRECT_MESSAGE]);
        }
    };
    
    auto onReadCfgSuccess = ^(encfs::EncfsConfig cfg) {
        showBlockingSheetMessage(self.window,
                                 [NSString stringWithUTF8String:LOCKBOX_PROGRESS_VERIFYING_PASS_TITLE],
                                 onVerifySuccess,
                                 onFail,
                                 ^{
                                     auto pass_is_correct = encfs::verify_password(cfg, password);
                                     return (pass_is_correct
                                             ? opt::make_optional(cfg)
                                             : opt::nullopt);
                                 });
    };
    
    showBlockingSheetMessage(self.window,
                             [NSString stringWithUTF8String:LOCKBOX_PROGRESS_READING_CONFIG_TITLE],
                             onReadCfgSuccess,
                             onFail,
                             encfs::read_config, self->fs, encrypted_container_path);
}

- (IBAction)cancelStart:(id)sender {
    [self.window performClose:sender];
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    
    // Implement this method to handle any initialization after your window controller's window has been loaded from its nib file.
}

@end

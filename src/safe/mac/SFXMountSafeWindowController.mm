//
//  SFXMountSafeWindowController.m
//  Safe
//
//  Created by Rian Hunter on 11/8/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXMountSafeWindowController.h>

#import <safe/mac/SFXProgressSheetController.h>
#import <safe/constants.h>
#import <safe/logging.h>
#import <safe/mac/keychain.hpp>
#import <safe/mount_safe_dialog_logic.hpp>
#import <safe/mac/util.hpp>

@implementation SFXMountSafeWindowController

- (id)initWithDelegate:(NSObject <SFXMountSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_
               fileURL:(NSURL *)fileURL {
    self = [self initWithWindowNibName:@"SFXMountSafeWindowController"];
    if (self) {
        self.delegate = del;
        self->fs = fs_;
        
        if (!fileURL) fileURL = [NSURL fileURLWithPath:NSHomeDirectory()];
        self.fileURL = fileURL;
    }
    
    return self;
}

- (id)initWithDelegate:(NSObject <SFXMountSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_ {
    return [self initWithDelegate:del
                               fs:fs_
                          fileURL:nil];
    
}

- (id)initWithDelegate:(NSObject <SFXMountSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_
                  path:(opt::optional<encfs::Path>)maybePath {
    return [self initWithDelegate:del
                               fs:fs_
                          fileURL:maybePath ? [NSURL fileURLWithPath:[NSString stringWithUTF8String:maybePath->c_str()]] : nil];
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
    [self.delegate mountSafeDone:self
                              mount:std::move(self->maybeMount)];
}

- (IBAction)confirmStart:(id)sender {
    (void) sender;
    
    encfs::SecureMem password([self.passwordSecureTextField.stringValue lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
    memcpy(password.data(),
           self.passwordSecureTextField.stringValue.UTF8String,
           password.size());
    
    auto error_msg =
        safe::verify_mount_safe_dialog_fields(self->fs,
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
                                [NSString stringWithUTF8String:SAFE_DIALOG_NO_CONFIG_EXISTS_TITLE],
                                [NSString stringWithUTF8String:SAFE_DIALOG_NO_CONFIG_EXISTS_MESSAGE]);
                alerted = true;
            }
            catch (const std::exception &err) {
                lbx_log_error("Error while mounting: %s", err.what());
            }
        }
        
        if (!alerted) {
            inputErrorAlert(self.window,
                            [NSString stringWithUTF8String:SAFE_DIALOG_UNKNOWN_MOUNT_ERROR_TITLE],
                            [NSString stringWithUTF8String:SAFE_DIALOG_UNKNOWN_MOUNT_ERROR_MESSAGE]);
        }
    };
    
    auto onMountSuccess = ^(safe::mac::MountDetails md) {
        self->maybeMount = std::move(md);
        [self.window performClose:self];
    };
    
    auto onVerifySuccess = ^(opt::optional<encfs::EncfsConfig> maybeConfig) {
        if (maybeConfig) {
            // password was correct, save to keychain if requested
            if (self.rememberPasswordCheckbox.state == NSOnState) {
                try {
                    safe::mac::save_password_for_location(self.locationPathControl.URL, self.passwordSecureTextField.stringValue);
                }
                catch (const std::exception & err) {
                    // error while saving password, oh well
                    // TODO: consider warning the user so they make sure to remember their password
                    //       the old-fashioned way
                    lbx_log_debug("couldn't save password to keychain: %s", err.what());
                }
            }
            
            self->maybeMount = [self.delegate takeMount:encrypted_container_path];
            if (self->maybeMount) {
                [self.window performClose:self];
                return;
            }
            
            // success pass values to app
            showBlockingSheetMessage(self.window,
                                     [NSString stringWithUTF8String:SAFE_PROGRESS_MOUNTING_EXISTING_TITLE],
                                     onMountSuccess,
                                     onFail,
                                     safe::mac::mount_new_encfs_drive,
                                     self->fs,
                                     encrypted_container_path, *maybeConfig, password);
        }
        else {
            inputErrorAlert(self.window,
                            [NSString stringWithUTF8String:SAFE_DIALOG_PASS_INCORRECT_TITLE],
                            [NSString stringWithUTF8String:SAFE_DIALOG_PASS_INCORRECT_MESSAGE]);
        }
    };
    
    auto onReadCfgSuccess = ^(encfs::EncfsConfig cfg) {
        showBlockingSheetMessage(self.window,
                                 [NSString stringWithUTF8String:SAFE_PROGRESS_VERIFYING_PASS_TITLE],
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
                             [NSString stringWithUTF8String:SAFE_PROGRESS_READING_CONFIG_TITLE],
                             onReadCfgSuccess,
                             onFail,
                             encfs::read_config, self->fs, encrypted_container_path);
}

- (IBAction)cancelStart:(id)sender {
    [self.window performClose:sender];
}

- (IBAction)locationURLChanged:(id)sender {
    (void)sender;
    NSURL *url = self.locationPathControl.URL;
    
    try {
        self.passwordSecureTextField.stringValue = safe::mac::get_saved_password_for_location(url);
    }
    catch (const std::exception & err) {
        lbx_log_error("Error while getting keychain password for %s (%s)",
                      url.path.fileSystemRepresentation, err.what());
    }
}

- (void)windowDidLoad
{
    [super windowDidLoad];

    if (self.fileURL) {
        self.locationPathControl.URL = self.fileURL;
        [self.window makeFirstResponder:self.passwordSecureTextField];
        [self locationURLChanged:nil];
    }

    safe::mac::initialize_window_for_dialog(self.window);
}

@end

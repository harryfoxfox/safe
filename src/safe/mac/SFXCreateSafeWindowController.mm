//
//  SFXCreateSafeWindowController.mm
//  Safe
//
//  Created by Rian Hunter on 11/6/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXCreateSafeWindowController.h>

#import <safe/mac/SFXProgressSheetController.h>
#import <safe/mac/SFXUnknownErrorSheet.hpp>
#import <safe/mac/keychain.hpp>
#import <safe/mac/mount.hpp>
#import <safe/mac/util.hpp>

#import <safe/report_exception.hpp>
#import <safe/constants.h>
#import <safe/create_safe_dialog_logic.hpp>

#import <encfs/fs/FileUtils.h>

#import <encfs/cipher/MemoryPool.h>

@implementation SFXCreateSafeWindowController

- (id)initWithDelegate:(NSObject <SFXCreateSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_ {
    self = [self initWithWindowNibName:@"SFXCreateSafeWindowController"];
    if (self) {
        self.delegate = del;
        self->fs = fs_;
    }
    
    return self;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
    [self.delegate createSafeDone:self
                               mount:std::move(self->maybeMount)];
}

- (void)inputErrorAlertWithTitle:(NSString *)title message:(NSString *)msg {
    inputErrorAlert(self.window, title, msg);
}

static
encfs::SecureMem
NSStringToSecureMem(NSString *str) {
    auto password_buf = encfs::SecureMem([str lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
    memcpy(password_buf.data(), str.UTF8String, password_buf.size());
    return std::move(password_buf);
}

- (void)unknownError:(const std::exception_ptr &)eptr {
    runUnknownErrorSheet(self.window,
                         safe::mac::to_ns_string(SAFE_DIALOG_UNKNOWN_CREATE_ERROR_TITLE),
                         safe::mac::to_ns_string(SAFE_DIALOG_UNKNOWN_CREATE_ERROR_MESSAGE),
                         safe::ExceptionLocation::CREATE,
                         eptr);
}

- (opt::optional<std::pair<encfs::Path, encfs::SecureMem>>) verifyFields {
    auto password_buf = NSStringToSecureMem(self.passwordSecureTextField.stringValue);
    auto confirm_password_buf = NSStringToSecureMem(self.confirmSecureTextField.stringValue);
    
    NSString *location = self.locationPathControl.URL.path;
    NSString *name = self.nameTextField.stringValue;
    auto error_msg =
        safe::verify_create_safe_dialog_fields(self->fs,
                                                     location.length ? location.fileSystemRepresentation : "",
                                                     name.length ? name.fileSystemRepresentation : "",
                                                     password_buf, confirm_password_buf);
    if (error_msg) {
        // location is bad
        [self inputErrorAlertWithTitle:[NSString stringWithUTF8String:error_msg->title.c_str()]
                               message:[NSString stringWithUTF8String:error_msg->message.c_str()]];
        return opt::nullopt;
    }

    NSURL *encrypted_container_url = [self.locationPathControl.URL
                                      URLByAppendingPathComponent:self.nameTextField.stringValue];

    // this method is not supposed to throw exceptions so since we're calling function
    // that can possibly throw exception, we catch it and alert the unknown error mechanism
    opt::optional<encfs::Path> maybe_encrypted_container_path;
    try {
        maybe_encrypted_container_path = safe::mac::url_to_path(self->fs, encrypted_container_url);
    }
    catch (...) {
        [self unknownError:std::current_exception()];
        return opt::nullopt;
    }

    return std::make_pair(std::move(*maybe_encrypted_container_path), std::move(password_buf));
}

- (IBAction)confirmCreate:(id)sender {
    (void)sender;

    auto maybeValidFields = [self verifyFields];
    if (!maybeValidFields) return;

    auto validFields = *maybeValidFields;

    auto encrypted_container_path = std::move(std::get<0>(validFields));
    auto password = std::move(std::get<1>(validFields));

    const auto use_case_safe_filename_encoding = true;

    auto onMountSuccess = ^(safe::mac::MountDetails md) {
        self->maybeMount = std::move(md);
        [self.window performClose:self];
    };

    auto onFail = ^(const std::exception_ptr & eptr) {
        // TODO: delete directory and .encfs.txt
        //       *only* if those are the only files that exist
        try {
            self->fs->rmdir(encrypted_container_path);
        }
        catch (const std::exception & err) {
            lbx_log_error("Error deleting encrypted container: %s", err.what());
        }

        [self unknownError:eptr];
    };

    auto onCreateCfgSuccess = ^(encfs::EncfsConfig cfg) {
        if (self.rememberPasswordCheckbox.state == NSOnState) {
            try {
                safe::mac::save_password_for_location([NSURL fileURLWithPath:safe::mac::to_ns_string(encrypted_container_path.c_str())], self.passwordSecureTextField.stringValue);
            }
            catch (const std::exception & err) {
                // error while saving password, oh well
                // TODO: consider warning the user so they make sure to remember their password
                //       the old-fashioned way
                lbx_log_debug("couldn't save password to keychain: %s", err.what());
            }
        }
        // success pass values to app
        showBlockingSheetMessage(self.window,
                                 [NSString stringWithUTF8String:SAFE_PROGRESS_MOUNTING_TITLE],
                                 onMountSuccess,
                                 onFail,
                                 safe::mac::mount_new_encfs_drive,
                                 self->fs,
                                 encrypted_container_path, cfg, password);
    };
    
    showBlockingSheetMessage(self.window,
                             [NSString stringWithUTF8String:SAFE_PROGRESS_CREATING_TITLE],
                             onCreateCfgSuccess,
                             onFail,
                             ^{
                               auto cfg =
                                 encfs::create_paranoid_config(password,
                                                               use_case_safe_filename_encoding);
                               self->fs->mkdir(encrypted_container_path);
                               encfs::write_config(self->fs, encrypted_container_path, cfg);
                               return cfg;
                             });
}

- (IBAction)cancelWindow:(id)sender {
    [self.window performClose:sender];
}

- (IBAction)rememberPasswordChecked:(id)sender {
    (void)sender;
    self.delegate.shouldRememberPassword = self.rememberPasswordCheckbox.state == NSOnState;
}

- (void)shouldRememberPasswordChanged {
    self.rememberPasswordCheckbox.state = self.delegate.shouldRememberPassword
    ? NSOnState
    : NSOffState;
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    
    self.locationPathControl.URL = [NSURL fileURLWithPath:NSHomeDirectory()];
    [self shouldRememberPasswordChanged];
    
    safe::mac::initialize_window_for_dialog(self.window);
}

@end

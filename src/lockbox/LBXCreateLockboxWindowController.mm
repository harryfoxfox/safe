//
//  LBXCreateLockboxWindowController.mm
//  Lockbox
//
//  Created by Rian Hunter on 11/6/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXCreateLockboxWindowController.h>

#import <lockbox/LBXProgressSheetController.h>
#import <lockbox/constants.h>
#import <lockbox/create_lockbox_dialog_logic.hpp>
#import <lockbox/mount_mac.hpp>
#import <lockbox/util_mac.hpp>

#import <encfs/fs/FileUtils.h>

#import <encfs/cipher/MemoryPool.h>

@implementation LBXCreateLockboxWindowController

- (id)initWithDelegate:(NSObject <LBXCreateLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_ {
    self = [self initWithWindowNibName:@"LBXCreateLockboxWindowController"];
    if (self) {
        self.delegate = del;
        self->fs = fs_;
    }
    
    return self;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    [self.window orderOut:self];
    [self.delegate createLockboxDone:self
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

- (opt::optional<std::pair<encfs::Path, encfs::SecureMem>>) verifyFields {
    auto password_buf = NSStringToSecureMem(self.passwordSecureTextField.stringValue);
    auto confirm_password_buf = NSStringToSecureMem(self.confirmSecureTextField.stringValue);
    
    NSString *location = self.locationPathControl.URL.path;
    NSString *name = self.nameTextField.stringValue;
    auto error_msg =
        lockbox::verify_create_lockbox_dialog_fields(self->fs,
                                                     location.length ? location.fileSystemRepresentation : "",
                                                     name.length ? name.fileSystemRepresentation : "",
                                                     password_buf, confirm_password_buf);
    if (error_msg) {
        // location is bad
        [self inputErrorAlertWithTitle:[NSString stringWithUTF8String:error_msg->title.c_str()]
                               message:[NSString stringWithUTF8String:error_msg->message.c_str()]];
        return opt::nullopt;
    }

    auto encrypted_container_path =
        self->fs->pathFromString(self.locationPathControl.URL.path.fileSystemRepresentation).join(self.nameTextField.stringValue.fileSystemRepresentation);

    return std::make_pair(std::move(encrypted_container_path), std::move(password_buf));
}


- (IBAction)confirmCreate:(id)sender {
    (void)sender;
    
    auto maybeValidFields = [self verifyFields];
    if (!maybeValidFields) return;
    
    auto validFields = *maybeValidFields;

    auto encrypted_container_path = std::move(std::get<0>(validFields));
    auto password = std::move(std::get<1>(validFields));
    
    const auto use_case_safe_filename_encoding = true;
    
    auto onFail = ^(const std::exception_ptr & eptr) {
        // TODO log eptr
        (void) eptr;
        
        // TODO: delete directory and .encfs.txt
        //       *only* if those are the only files that exist
        try {
            self->fs->rmdir(encrypted_container_path);
        }
        catch (...) {
            //TODO: log this error
        }
        
        [self inputErrorAlertWithTitle:[NSString stringWithUTF8String:LOCKBOX_DIALOG_UNKNOWN_CREATE_ERROR_TITLE]
                               message:[NSString stringWithUTF8String:LOCKBOX_DIALOG_UNKNOWN_CREATE_ERROR_MESSAGE]];
    };
    
    auto onMountSuccess = ^(lockbox::mac::MountDetails md) {
        self->maybeMount = std::move(md);
        [self.window performClose:self];
    };
    
    auto onCreateCfgSuccess = ^(encfs::EncfsConfig cfg) {
        // success pass values to app
        showBlockingSheetMessage(self.window,
                                 [NSString stringWithUTF8String:LOCKBOX_PROGRESS_MOUNTING_TITLE],
                                 onMountSuccess,
                                 onFail,
                                 lockbox::mac::mount_new_encfs_drive,
                                 self->fs,
                                 encrypted_container_path, cfg, password);
    };
    
    showBlockingSheetMessage(self.window,
                             [NSString stringWithUTF8String:LOCKBOX_PROGRESS_CREATING_TITLE],
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

- (void)windowDidLoad
{
    [super windowDidLoad];
    
    self.locationPathControl.URL = [NSURL fileURLWithPath:NSHomeDirectory()];
    
    lockbox::mac::initialize_window_for_dialog(self.window);
}

@end

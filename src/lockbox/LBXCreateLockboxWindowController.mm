//
//  LBXCreateLockboxWindowController.mm
//  Lockbox
//
//  Created by Rian Hunter on 11/6/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXCreateLockboxWindowController.h>
#import <lockbox/LBXProgressSheetController.h>

#include <lockbox/mount_mac.hpp>

#include <encfs/fs/FileUtils.h>

#include <encfs/cipher/MemoryPool.h>

@interface LBXCreateLockboxWindowController ()

@end

@implementation LBXCreateLockboxWindowController

- (id)initWithWindow:(NSWindow *)window
{
    self = [super initWithWindow:window];
    if (self) {
        // Initialization code here.
    }
    return self;
}

- (id)initWithDelegate:(NSObject <LBXCreateLockboxWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs_ {
    NSLog(@"sup");

    self = [self initWithWindowNibName:@"LBXCreateLockboxWindowController"];
    if (self) {
        self.delegate = del;
        self->fs = fs_;
        
        // load window
        (void) self.window;
        
        self.locationPathControl.URL = [NSURL fileURLWithPath:NSHomeDirectory()];
        
        self.window.canHide = NO;
        [self.window center];
        [self.window makeKeyAndOrderFront:self];
        self.window.level = NSModalPanelWindowLevel;
    }
    
    return self;
}

- (void)inputErrorAlertWithTitle:(NSString *)title message:(NSString *)msg {
    inputErrorAlert(self.window, title, msg);
}

- (opt::optional<std::pair<encfs::Path, encfs::SecureMem>>) verifyFields {
    // check if this location is a well-formed path
    opt::optional<encfs::Path> maybeLocationPath;
    try {
        maybeLocationPath = self->fs->pathFromString(self.locationPathControl.URL.path.fileSystemRepresentation);
    }
    catch (...) {
        // location is bad
        [self inputErrorAlertWithTitle:@"Bad Location"
                               message:@"The location you have chosen is invalid."];
        return opt::nullopt;
    }
    
    auto locationPath = std::move(*maybeLocationPath);
    
    // check if location exists
    if (!encfs::file_exists(self->fs, locationPath)) {
        [self inputErrorAlertWithTitle:@"Location does not exist"
                               message:@"The location you have chosen does not exist."];
        return opt::nullopt;
    }
    
    // check validity of name field
    NSString *name = self.nameTextField.stringValue;
    
    if (![name length]) {
        [self inputErrorAlertWithTitle:@"Name is Empty"
                               message:@"The name you have entered is empty. Please enter a non-empty name."];
        return opt::nullopt;
    }
    
    opt::optional<encfs::Path> maybeContainerPath;
    try {
        maybeContainerPath = locationPath.join(name.fileSystemRepresentation);
    }
    catch (...) {
        [self inputErrorAlertWithTitle:@"Bad Name"
                               message:@"The name you have entered is not valid for a folder name. Please choose a name that's more appropriate for a folder, try not using special characters."];
        return opt::nullopt;
    }
    
    auto containerPath = *maybeContainerPath;
    
    if (encfs::file_exists(self->fs, containerPath)) {
        // path already exists
        [self inputErrorAlertWithTitle:@"File Already Exists"
                               message:@"A file already exists with the name you have chosen, please choose another name."];
        return opt::nullopt;
    }
    
    // check validity of password
    NSString *password = self.passwordSecureTextField.stringValue;
    
    if (![password length]) {
        [self inputErrorAlertWithTitle:@"Password is Empty"
                               message:@"The password you have entered is empty. Please enter a non-empty password."];
        return opt::nullopt;
    }
    
    NSString *passwordConfirm = self.confirmSecureTextField.stringValue;
    
    if (![password isEqualToString:passwordConfirm]) {
        [self inputErrorAlertWithTitle:@"Passwords Don't Match"
                               message:@"The passwords you entered don't match. Please enter matching passwords for the \"Password\" and \"Confirm\" fields."];
        return opt::nullopt;
    }
    
    auto mem = encfs::SecureMem([password lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
    memcpy(mem.data(), password.UTF8String, mem.size());

    return std::make_pair(std::move(containerPath), std::move(mem));
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
        
        [self inputErrorAlertWithTitle:@"Unknown Error"
                               message:@"Unknown error occurred while creating new Bitvault"];
    };
    
    auto onMountSuccess = ^(lockbox::mac::MountDetails md) {
        [self.delegate createLockboxDone:self mount:std::move(md)];
    };
    
    auto onSaveCfgSuccess = ^(encfs::EncfsConfig cfg) {
        // success pass values to app
        showBlockingSheetMessage(self.window,
                                 @"Starting new Bitvault",
                                 onMountSuccess,
                                 onFail,
                                 lockbox::mac::mount_new_encfs_drive,
                                 self->fs,
                                 encrypted_container_path, cfg, password);
    };
    
    auto onCreateCfgSuccess = ^(const encfs::EncfsConfig & cfg_) {
        auto cfg = cfg_;
        showBlockingSheetMessage(self.window,
                                 @"Saving new configuration...",
                                 onSaveCfgSuccess,
                                 onFail,
                                 ^{
                                     self->fs->mkdir(encrypted_container_path);
                                     encfs::write_config(self->fs, encrypted_container_path, cfg);
                                     return cfg;
                                 });
    };
    
    showBlockingSheetMessage(self.window,
                             @"Creating new configuration...",
                             onCreateCfgSuccess,
                             onFail,
                             encfs::create_paranoid_config, password, use_case_safe_filename_encoding);
}

- (IBAction)cancelWindow:(id)sender {
    (void)sender;
    [self.delegate createLockboxCanceled:self];
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    
    // Implement this method to handle any initialization after your window controller's window has been loaded from its nib file.
}

@end

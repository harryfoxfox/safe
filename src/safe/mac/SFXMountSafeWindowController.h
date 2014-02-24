//
//  SFXMountSafeWindowController.h
//  Safe
//
//  Created by Rian Hunter on 11/8/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <safe/mac/mount.hpp>

#import <Cocoa/Cocoa.h>

@class SFXMountSafeWindowController;

@protocol SFXMountSafeWindowControllerDelegate <NSObject>

- (void)mountSafeDone:(SFXMountSafeWindowController *)wc
                   mount:(opt::optional<safe::mac::MountDetails>)a;

- (opt::optional<safe::mac::MountDetails>)takeMount:(const encfs::Path &)p;
- (bool)hasMount:(const encfs::Path &)p;


@end

@interface SFXMountSafeWindowController : NSWindowController <NSWindowDelegate> {
    std::shared_ptr<encfs::FsIO> fs;
    opt::optional<safe::mac::MountDetails> maybeMount;
}

@property (nonatomic, weak) NSObject <SFXMountSafeWindowControllerDelegate> *delegate;
@property (retain) NSURL *fileURL;

@property (weak) IBOutlet NSPathControl *locationPathControl;
@property (weak) IBOutlet NSButton *rememberPasswordCheckbox;
@property (weak) IBOutlet NSButton *showPasswordCheckbox;
@property (weak) IBOutlet NSTextField *insecurePasswordTextField;
@property (weak) IBOutlet NSSecureTextField *securePasswordTextField;
@property (weak) IBOutlet NSTabView *passwordTabView;

- (id)initWithDelegate:(NSObject <SFXMountSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs;

- (id)initWithDelegate:(NSObject <SFXMountSafeWindowControllerDelegate> *) del
                    fs:(std::shared_ptr<encfs::FsIO>)fs
                  path:(opt::optional<encfs::Path>)maybePath;

- (IBAction)confirmStart:(id)sender;
- (IBAction)cancelStart:(id)sender;
- (IBAction)locationURLChanged:(id)sender;
- (IBAction)showPassword:(id)sender;

@end

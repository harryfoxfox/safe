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
#import <safe/report_exception.hpp>

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

- (void)unknownErrorResponse:(NSAlert *)alert
                  returnCode:(NSInteger)returnCode
                 contextInfo:(void *)contextInfo {
    (void) alert;
    auto ctx = std::unique_ptr<std::exception_ptr>((std::exception_ptr *) contextInfo);
    if (returnCode == NSAlertSecondButtonReturn) {
        safe::report_exception(safe::ExceptionLocation::MOUNT, *ctx);
    }
    [alert.window orderOut:self];
    [self.window performClose:self];
}

- (void)unknownError:(const std::exception_ptr &)eptr {
    // unknown error occured, allow user to report
    auto message = (std::string(SAFE_DIALOG_UNKNOWN_MOUNT_ERROR_MESSAGE) +
                    (" Please help us improve by sending a bug report. It's automatic and "
                     "no personal information is used."));

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:safe::mac::to_ns_string(SAFE_DIALOG_UNKNOWN_MOUNT_ERROR_TITLE)];
    [alert setInformativeText:safe::mac::to_ns_string(message)];
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Report Bug"];
    [alert setAlertStyle:NSWarningAlertStyle];

    [alert beginSheetModalForWindow:self.window
                      modalDelegate:self
                     didEndSelector:@selector(unknownErrorResponse:returnCode:contextInfo:)
                        contextInfo:(void *) new std::exception_ptr(eptr)];
}

- (NSTextField *)currentPasswordView {
    if (self.showPasswordCheckbox.state == NSOnState) {
        return self.insecurePasswordTextField;
    }
    else {
        return self.securePasswordTextField;
    }
}

- (NSString *)password {
    return self.currentPasswordView.stringValue;
}

- (void)setPassword:(NSString *)p {
    self.currentPasswordView.stringValue = p;
}

- (IBAction)confirmStart:(id)sender {
    (void) sender;
    
    encfs::SecureMem password([self.password lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
    memcpy(password.data(),
           self.password.UTF8String,
           password.size());
    
    auto error_msg =
        safe::verify_mount_safe_dialog_fields(self->fs,
                                              self.locationPathControl.URL.path.fileSystemRepresentation,
                                              password);
    if (error_msg) {
        inputErrorAlert(self.window,
                        [NSString stringWithUTF8String:error_msg->title.c_str()],
                        [NSString stringWithUTF8String:error_msg->message.c_str()]);
        return;
    }

    auto onFail = ^(const std::exception_ptr & eptr) {
        bool alerted = false;
        try {
            std::rethrow_exception(eptr);
        }
        catch (const encfs::BadPassword &) {
            inputErrorAlert(self.window,
                            safe::mac::to_ns_string(SAFE_DIALOG_PASS_INCORRECT_TITLE),
                            safe::mac::to_ns_string(SAFE_DIALOG_PASS_INCORRECT_MESSAGE));
            alerted = true;
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

        if (!alerted) [self unknownError:eptr];
    };

    opt::optional<encfs::Path> maybe_encrypted_container_path;

    try {
        maybe_encrypted_container_path = safe::mac::url_to_path(self->fs, self.locationPathControl.URL);
    }
    catch (...) {
        onFail(std::current_exception());
        return;
    }

    auto encrypted_container_path = std::move(*maybe_encrypted_container_path);
    
    auto onMountSuccess = ^(safe::mac::MountDetails md) {
        // password was correct, save to keychain if requested
        if (self.rememberPasswordCheckbox.state == NSOnState) {
            try {
                safe::mac::save_password_for_location(self.locationPathControl.URL, self.password);
            }
            catch (const std::exception & err) {
                // error while saving password, oh well
                // TODO: consider warning the user so they make sure to remember their password
                //       the old-fashioned way
                lbx_log_debug("couldn't save password to keychain: %s", err.what());
            }
        }
        
        self->maybeMount = std::move(md);
        [self.window performClose:self];
    };
    
    auto onReadCfgSuccess = ^(encfs::EncfsConfig cfg) {
        showBlockingSheetMessage(self.window,
                                 [NSString stringWithUTF8String:SAFE_PROGRESS_MOUNTING_EXISTING_TITLE],
                                 onMountSuccess,
                                 onFail,
                                 ^{
                                     if ([self.delegate hasMount:encrypted_container_path]) {
                                         auto pass_is_correct = encfs::verify_password(cfg, password);
                                         if (!pass_is_correct) throw encfs::BadPassword();
                                         return std::move(*[self.delegate takeMount:encrypted_container_path]);
                                     }
                                     else {
                                         return safe::mac::mount_new_encfs_drive(self->fs,
                                                                                 encrypted_container_path,
                                                                                 cfg, password);
                                     }
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

    NSString *pass = nil;
    try {
        pass = safe::mac::get_saved_password_for_location(url);
    }
    catch (const std::exception & err) {
        lbx_log_error("Error while getting keychain password for %s (%s)",
                      url.path.fileSystemRepresentation, err.what());
    }

    if (pass) self.password = pass;
}

- (IBAction)showPassword:(id)sender {
    (void) sender;
    bool should_show_password =
        self.showPasswordCheckbox.state == NSOnState;

    NSTextField *lastTextField = should_show_password
        ? self.securePasswordTextField
        : self.insecurePasswordTextField;

    // get current cursor location
    NSText *fieldEditor = [self.window fieldEditor:YES forObject:lastTextField];
    NSRange curRange = fieldEditor.selectedRange;

    // transfer password value
    self.password = lastTextField.stringValue;

    // switch views
    [self.passwordTabView selectTabViewItemAtIndex:
     should_show_password ? 0 : 1];

    // reset cursor location (after switching views, since that also manipulates cursor position)
    NSText *newFieldEditor = [self.window fieldEditor:YES forObject:self.currentPasswordView];
    newFieldEditor.selectedRange = curRange;
}

- (IBAction)rememberPasswordChecked:(id)sender {
    (void) sender;
    self.delegate.shouldRememberPassword = self.rememberPasswordCheckbox.state == NSOnState;
}

- (void)windowDidLoad
{
    [super windowDidLoad];

    [self showPassword:nil];

    if (self.fileURL) {
        self.locationPathControl.URL = self.fileURL;
        [self.window makeFirstResponder:self.currentPasswordView];
        [self locationURLChanged:nil];
    }

    self.rememberPasswordCheckbox.state = self.delegate.shouldRememberPassword
    ? NSOnState
    : NSOffState;

    safe::mac::initialize_window_for_dialog(self.window);
}

@end

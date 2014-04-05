//
//  SFXAppDelegate.mm
//  Safe
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <safe/mac/SFXAppDelegate.h>

#import <safe/mac/SFXProgressSheetController.h>

#import <safe/constants.h>
#import <safe/fs.hpp>
#import <safe/logging.h>
#import <safe/mac/mount.hpp>
#import <safe/parse.hpp>
#import <safe/mac/RecentlyUsedNSURLStoreV1.hpp>
#import <safe/mac/shared_file_list.hpp>
#import <safe/mac/system_changes.hpp>
#import <safe/tray_menu.hpp>
#import <safe/util.hpp>
#import <safe/mac/util.hpp>
#import <safe/webdav_server.hpp>
#import <safe/report_exception.hpp>

#import <encfs/base/logging.h>

// 10 to model after system mac recent menus
static NSString *const SFX_ACTION_KEY = @"_lbx_action";
static NSString *const SAFE_MAC_LOCK_FILE = @"LockFile";
static NSString *const SAFE_LIST_ITEM_OWNER = safe::mac::to_ns_string(PRODUCT_NAME_A);

class MacOSXTrayMenuItem {
private:
    NSMenuItem *_menu_item;

public:
    MacOSXTrayMenuItem(NSMenuItem *menu_item)
    : _menu_item(menu_item) {}
    
    bool
    set_tooltip(std::string tooltip) {
        _menu_item.toolTip = [NSString stringWithUTF8String:tooltip.c_str()];
        return false;
    }
    
    bool
    set_property(safe::TrayMenuProperty name, std::string value) {
        // we don't support menu tooltips
        if (name == safe::TrayMenuProperty::MAC_FILE_TYPE) {
            _menu_item.image = [NSWorkspace.sharedWorkspace
                                iconForFileType:[NSString stringWithUTF8String:value.c_str()]];
            if (!_menu_item.image) throw std::runtime_error("bad icon name");
            _menu_item.image.size = NSMakeSize(16, 16);
            return true;
        }
        return false;
    }
    
    void
    set_checked(bool checked) {
        _menu_item.state = checked ? NSOnState : NSOffState;
    }
    
    void
    disable() {
        _menu_item.enabled = NO;
    }
};

class MacOSXTrayMenu {
    NSMenu *_menu;
    SEL _sel;
    id _target;
    
public:
    MacOSXTrayMenu(NSMenu *menu, id target, SEL sel)
    : _menu(menu)
    , _target(target)
    , _sel(sel) {
        _menu.autoenablesItems = FALSE;
    }
    
    MacOSXTrayMenuItem
    append_item(std::string title,
                safe::TrayMenuAction action,
                safe::tray_menu_action_arg_t action_arg = 0) {
        NSMenuItem *mi = [NSMenuItem.alloc initWithTitle:[NSString stringWithUTF8String:title.c_str()]
                                                  action:_sel
                                           keyEquivalent:@""];
        mi.target = _target;
        mi.tag = safe::encode_menu_id<NSInteger>(action, action_arg);
        [_menu addItem:mi];
        return MacOSXTrayMenuItem(mi);
    }
    
    void
    append_separator() {
        [_menu addItem:NSMenuItem.separatorItem];
    }
    
    MacOSXTrayMenu
    append_menu(std::string title) {
        NSMenu *recentSubmenu = NSMenu.alloc.init;
        NSMenuItem *mi = [NSMenuItem.alloc initWithTitle:[NSString stringWithUTF8String:title.c_str()]
                                                  action:nil
                                           keyEquivalent:@""];
        mi.submenu = recentSubmenu;
        [_menu addItem:mi];
        return MacOSXTrayMenu(recentSubmenu, _target, _sel);
    }
};

static
bool
app_is_run_at_login() {
    return safe::mac::shared_file_list_contains_live_item_with_owner(kLSSharedFileListSessionLoginItems,
                                                                     SAFE_LIST_ITEM_OWNER);
}

static
bool
set_app_to_run_at_login(bool run_at_login) {
    auto appPath = NSBundle.mainBundle.bundlePath;
    NSURL *appURL = [NSURL fileURLWithPath:appPath];

    // clear all other login items owned by us, this makes sure the list is normalized to
    // run at most one "Safe" app at login
    bool removed = safe::mac::remove_items_with_owner_from_shared_file_list(kLSSharedFileListSessionLoginItems,
                                                                            SAFE_LIST_ITEM_OWNER);

    if (run_at_login) {
        safe::mac::add_url_with_owner_to_shared_file_list(kLSSharedFileListSessionLoginItems,
                                                          appURL, SAFE_LIST_ITEM_OWNER);
        return removed;
    }
    else {
        bool removed2 =
            safe::mac::remove_url_with_owner_from_shared_file_list(kLSSharedFileListSessionLoginItems,
                                                                   appURL, safe::mac::kSFXAnyOwner);
        return removed || removed2;
    }
}

static
void
normalize_app_is_run_at_login_setting() {
    // the purpose of this method is to make sure we only have one Safe entry
    // in the login items list and to make sure it now points to us (if there was one)
    bool removed = set_app_to_run_at_login(false);
    if (removed) set_app_to_run_at_login(true);
}

static
void
add_mount_to_favorites(const safe::mac::MountDetails & mount) {
    return safe::mac::add_url_with_owner_to_shared_file_list(kLSSharedFileListFavoriteVolumes,
                                                             [NSURL
                                                              fileURLWithPath:safe::mac::to_ns_string(mount.get_mount_point())
                                                              isDirectory:YES], SAFE_LIST_ITEM_OWNER);

}

static
bool
remove_mount_from_favorites(const safe::mac::MountDetails & mount) {
    return safe::mac::remove_url_with_owner_from_shared_file_list(kLSSharedFileListFavoriteVolumes,
                                                                  [NSURL
                                                                   fileURLWithPath:safe::mac::to_ns_string(mount.get_mount_point())
                                                                   isDirectory:YES],
                                                                  SAFE_LIST_ITEM_OWNER);
}

@implementation SFXAppDelegate

- (bool)hasMount:(const encfs::Path &)p {
    auto it = std::find_if(self->mounts.begin(), self->mounts.end(),
                           [&] (const safe::mac::MountDetails & md) {
                               return md.get_source_path() == p;
                           });
    return (it != self->mounts.end());
}

- (opt::optional<safe::mac::MountDetails>)takeMount:(const encfs::Path &)p {
    auto it = std::find_if(self->mounts.begin(), self->mounts.end(),
                           [&] (const safe::mac::MountDetails & md) {
                               return md.get_source_path() == p;
                           });
    if (it == self->mounts.end()) return opt::nullopt;

    auto md = std::move(*it);
    self->mounts.erase(it);
    return std::move(md);
}

+ (NSURL *)applicationSupportDirectoryError:(NSError **)err {
    NSURL *p = [NSFileManager.defaultManager
                URLForDirectory:NSApplicationSupportDirectory
                inDomain:NSUserDomainMask
                appropriateForURL:nil
                create:YES error:err];
    if (!p) return nil;
    
    NSString *executableName = safe::mac::to_ns_string(PRODUCT_NAME_A);
    
    NSURL *ourAppDirectory = [p URLByAppendingPathComponent:executableName];

    NSError *createErr;
    BOOL created = [NSFileManager.defaultManager
                    createDirectoryAtURL:ourAppDirectory
                    withIntermediateDirectories:NO
                    attributes:@{NSFilePosixPermissions: @0700}
                    error:&createErr];
    if (!created &&
        (![createErr.domain isEqualToString:NSPOSIXErrorDomain] ||
         createErr.code != EEXIST) &&
        (![createErr.domain isEqualToString:NSCocoaErrorDomain] ||
         createErr.code != NSFileWriteFileExistsError)) {
        if (err) *err = createErr;
        return nil;
    }
    
    return ourAppDirectory;
}

- (void)saveCurrentlyActive:(NSNotification *)notification {
    NSRunningApplication *app = [notification.userInfo objectForKey:NSWorkspaceApplicationKey];
    if (![app isEqual:NSRunningApplication.currentApplication]) {
        self.lastActiveApp = app;
    }
}

- (void)restoreLastActive {
    if (!self.createWindows.count && !self.mountWindows.count && !self.welcomeWindowDelegate &&
        (!self.aboutWindowController || !self.aboutWindowController.window.isVisible)) {
        [self.lastActiveApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    }
}

- (std::vector<safe::mac::MountDetails>::iterator)stopDrive:(std::vector<safe::mac::MountDetails>::iterator)it
                                                        withUI:(BOOL)ui {
    auto mount_name = it->get_mount_name();

    remove_mount_from_favorites(*it);
    it = self->mounts.erase(it);
    [self _updateStatusMenu];
    
    if (ui) {
        [self notifyUserTitle:@"Success"
                      message:[NSString stringWithFormat:@"You've successfully unmounted \"%s.\"",
                               mount_name.c_str(), nil]];
    }
    
    return it;
}

- (std::vector<safe::mac::MountDetails>::iterator)unmountAndStopDrive:(std::vector<safe::mac::MountDetails>::iterator)it
                                                                  withUI:(BOOL)ui {
    it->unmount();
    return [self stopDrive:it withUI:ui];
}

- (void)_newMount:(safe::mac::MountDetails)md {
    self->path_store->use_path(md.get_source_path());
    self->mounts.insert(self->mounts.begin(), std::move(md));
    const auto & new_mount = self->mounts.front();
    add_mount_to_favorites(new_mount);
    [self _updateStatusMenu];
    new_mount.open_mount();
    [self notifyUserTitle:@"Success"
                  message:[NSString stringWithFormat:@"You've successfully mounted \"%s.\"",
                           new_mount.get_mount_name().c_str(), nil]];
}

- (void)createSafeDone:(SFXCreateSafeWindowController *)wc
                    mount:(opt::optional<safe::mac::MountDetails>)maybeMount {
    [self.createWindows removeObject:wc];
    if (maybeMount) [self _newMount:std::move(*maybeMount)];
    else [self restoreLastActive];
}

- (void)mountSafeDone:(SFXMountSafeWindowController *)wc
                   mount:(opt::optional<safe::mac::MountDetails>)maybeMount {
    [self.mountWindows removeObject:wc];
    if (maybeMount) [self _newMount:std::move(*maybeMount)];
    else [self restoreLastActive];
}

- (void)openMountDialogForPath:(opt::optional<encfs::Path>)maybePath {
    SFXMountSafeWindowController *wc = [SFXMountSafeWindowController.alloc
                                           initWithDelegate:self
                                           fs:self->native_fs
                                           path:std::move(maybePath)];
    [self.mountWindows addObject:wc];
    [wc showWindow:nil];
    // NB: request activation *after* showing the window, if we do it beforehand
    //     we may lose activation privilege if a keychain dialog popups before
    //     our window is actually shown (we load from keychain in the window init)
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
}

- (void)mountNthMostRecentlyMounted:(size_t)n {
    auto resolver = (*self->path_store)[n];
    opt::optional<encfs::Path> path_resolution;
    try {
        path_resolution = std::get<0>(resolver.resolve_path());
    }
    catch (const std::exception & err) {
        // failure to resolve bookmark
        // TODO: show error, for now do nothing
        lbx_log_debug("Couldn't resolve bookmark: %s", err.what());
    }

    if (path_resolution) {
        [self openMountDialogForPath:path_resolution];
    }
}

- (void)createNewSafe:(id)sender {
    (void)sender;
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
    SFXCreateSafeWindowController *wc = [[SFXCreateSafeWindowController alloc]
                                            initWithDelegate:self fs:self->native_fs];
    [self.createWindows addObject:wc];
    [wc showWindow:nil];
}

- (void)mountExistingSafe:(id)sender {
    (void)sender;
    [self openMountDialogForPath:opt::nullopt];

}

- (void)welcomeWindowDone:(id)sender withAction:(welcome_window_action_t)action{
    (void) sender;
    self.welcomeWindowDelegate = nil;
    if (action == WELCOME_WINDOW_MOUNT) {
        [self mountExistingSafe:self];
    }
    else if (action == WELCOME_WINDOW_CREATE) {
        [self createNewSafe:self];
    }
    else {
        [self restoreLastActive];
    }
}

// NB: this throws exception like a C++ function, even though it's objective-c
//     we should really refactor the core functionality out of SFXAppDelegate into
//     a C++ class
- (void)__dispatch_tray_action:(NSInteger) tag {
    using safe::TrayMenuAction;

    TrayMenuAction menu_action;
    safe::tray_menu_action_arg_t menu_action_arg;
    
    std::tie(menu_action, menu_action_arg) = safe::decode_menu_id(tag);
    
    switch (menu_action) {
        case TrayMenuAction::CREATE: {
            [self createNewSafe:self];
            break;
        }
        case TrayMenuAction::MOUNT: {
            [self mountExistingSafe:self];
            break;
        }
        case TrayMenuAction::TOGGLE_RUN_AT_LOGIN: {
            set_app_to_run_at_login(!app_is_run_at_login());
            break;
        }
        case TrayMenuAction::ABOUT_APP: {
            [self _loadAboutWindow];
            break;
        }
        case TrayMenuAction::TRIGGER_BREAKPOINT: {
            Debugger();
            break;
        }
        case TrayMenuAction::TEST_BUBBLE: {
            [self notifyUserTitle:[NSString stringWithUTF8String:SAFE_NOTIFICATION_TEST_TITLE]
                          message:[NSString stringWithUTF8String:SAFE_NOTIFICATION_TEST_MESSAGE]];
            break;
        }
        case TrayMenuAction::QUIT_APP: {
            bool actually_quit = true;
            if (!self->mounts.empty()) {
                [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
                NSAlert *alert = [NSAlert alertWithMessageText:[NSString stringWithUTF8String:SAFE_DIALOG_QUIT_CONFIRMATION_TITLE]
                                                 defaultButton:@"Quit"
                                               alternateButton:@"Cancel"
                                                   otherButton:nil
                                     informativeTextWithFormat:[NSString stringWithUTF8String:SAFE_DIALOG_QUIT_CONFIRMATION_MESSAGE], nil];
                actually_quit = [alert runModal] == NSAlertDefaultReturn;
            }
            if (actually_quit) [NSApp terminate:self];
            break;
        }
        case TrayMenuAction::OPEN: {
            if (menu_action_arg >= self->mounts.size()) return;
            try {
                self->mounts[menu_action_arg].open_mount();
            }
            catch (const std::exception & err) {
                lbx_log_error("Error while opening mount: %s", err.what());
            }
            break;
        }
        case TrayMenuAction::UNMOUNT: {
            if (menu_action_arg >= self->mounts.size()) return;
            auto it = self->mounts.begin() + menu_action_arg;
            try {
                [self unmountAndStopDrive:it withUI:YES];
            }
            catch (const std::exception & err) {
                lbx_log_debug("Error while attempting to unmount \"%s\": %s",
                              it->get_mount_point().c_str(), err.what());
                [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
                NSAlert *alert =
                    [NSAlert alertWithMessageText:@"Unable to Unmount"
                                    defaultButton:nil
                                  alternateButton:nil
                                      otherButton:nil
                        informativeTextWithFormat:(@"Safe could not unmount \"%s.\" It's most likely in use. "
                                                   @"Please close all applications using files in \"%s\" before "
                                                   @"attempting to unmount again."),
                     it->get_mount_name().c_str(), it->get_mount_name().c_str()
                     ];
                [alert beginSheetModalForWindow:nil modalDelegate:nil didEndSelector:nil contextInfo:nil];
            }

            break;
        }
        case TrayMenuAction::CLEAR_RECENTS: {
            self->path_store->clear();
            [self _updateStatusMenu];
            break;
        }
        case TrayMenuAction::MOUNT_RECENT: {
            if (menu_action_arg >= self->path_store->size()) return;
            [self mountNthMostRecentlyMounted:menu_action_arg];
            break;
        }
        case TrayMenuAction::SEND_FEEDBACK: {
            safe::mac::open_url(SAFE_SEND_FEEDBACK_WEBSITE);
            break;
        }
        case TrayMenuAction::THROW_EXCEPTION: {
            throw std::runtime_error("TrayMenuAction::THROW_EXCEPTION");
            break;
        }
        default: {
            /* should never happen */
            assert(false);
            lbx_log_warning("Bad tray action: %d", (int) menu_action);
            break;
        }
    }
}

- (void)_dispatchMenu:(id)sender {
    NSMenuItem *mi = sender;
    [self __dispatch_tray_action:mi.tag];
}

- (void)_updateStatusMenu {
    NSMenu *menu = self.statusItem.menu;
    
    [menu removeAllItems];
    
    bool show_alternative_menu = self->lastModifierFlags & NSAlternateKeyMask;
    auto m = MacOSXTrayMenu(menu, self, @selector(_dispatchMenu:));
    auto startup_item_is_enabled = app_is_run_at_login();
    safe::populate_tray_menu(m, self->mounts, *self->path_store,
                             startup_item_is_enabled,
                             show_alternative_menu);
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    (void)menu;
    assert(menu = self.statusItem.menu);
    self->lastModifierFlags = NSEvent.modifierFlags;
    // update menu based on modifier key
    [self _updateStatusMenu];
}

- (void)_setupStatusBar {
    NSMenu *statusMenu = [[NSMenu alloc] init];
    statusMenu.delegate = self;
    
    NSStatusBar *sb = [NSStatusBar systemStatusBar];
    self.statusItem = [sb statusItemWithLength:NSVariableStatusItemLength];
    
    self.statusItem.image = [NSImage imageNamed:@"menuBarIconTemplate"];
    if (self.statusItem.image.size.width == 16) {
        // NB: 16x16 icons are positioned at 2 px from the top of the menu bar
        // we'd rather position at 3 px from the top
        // so create a new image with 16x17 dimensions
        // with the image starting at 1px from the top
        NSImage *oldImage = self.statusItem.image;
        self.statusItem.image = [NSImage.alloc initWithSize:NSMakeSize(16, 17)];
        [self.statusItem.image lockFocus];
        [oldImage drawAtPoint:NSMakePoint(0, 0) fromRect:NSZeroRect operation:NSCompositeCopy fraction:1.0];
        [self.statusItem.image unlockFocus];
        [self.statusItem.image setTemplate:YES];

    }
    self.statusItem.highlightMode = YES;
    self.statusItem.menu = statusMenu;
    self.statusItem.toolTip = [NSString stringWithUTF8String:SAFE_TRAY_ICON_TOOLTIP];
    
    [self _updateStatusMenu];
}

void PostMouseEvent(CGMouseButton button, CGEventType type, const CGPoint point)
{
    CGEventRef theEvent = CGEventCreateMouseEvent(NULL, type, point, button);
    CGEventSetType(theEvent, type);
    CGEventPost(kCGHIDEventTap, theEvent);
    CFRelease(theEvent);
}

- (void)clickStatusItem {
    NSWindow *statusItemWindow;
    for (NSWindow *w in [NSApplication.sharedApplication windows]) {
        if (!w.title.length) {
            // window without a title, assuming this is our status bar window
            statusItemWindow = w;
            break;
        }
    }
    NSRect f = statusItemWindow.frame;
    CGFloat x_to_press = (f.origin.x + f.size.width/2);
    assert(!NSStatusBar.systemStatusBar.isVertical);
    CGPoint to_press = {x_to_press, NSStatusBar.systemStatusBar.thickness / 2};
    PostMouseEvent(kCGMouseButtonLeft, kCGEventLeftMouseDown, to_press);
}

- (BOOL)haveUserNotifications {
    return NSClassFromString(@"NSUserNotificationCenter") ? YES : NO;
}

- (void)notifyUserTitle:(NSString *)title
                message:(NSString *)msg
                 action:(SEL) sel {
    if (![self haveUserNotifications]) return;
    
    NSUserNotification *user_not = [[NSUserNotification alloc] init];
    user_not.title = title;
    user_not.informativeText = msg;
    user_not.deliveryDate = [NSDate dateWithTimeInterval:0 sinceDate:NSDate.date];
    user_not.soundName = NSUserNotificationDefaultSoundName;
    
    if (sel) {
        user_not.userInfo = @{SFX_ACTION_KEY: NSStringFromSelector(sel)};
    }

    [NSUserNotificationCenter.defaultUserNotificationCenter
     deliverNotification:user_not];
}

- (void)notifyUserTitle:(NSString *)title
                message:(NSString *)msg {
    [self notifyUserTitle:title message:msg action:nil];
}

#define SuppressPerformSelectorLeakWarning(Stuff) \
do { \
_Pragma("clang diagnostic push") \
_Pragma("clang diagnostic ignored \"-Warc-performSelector-leaks\"") \
Stuff; \
_Pragma("clang diagnostic pop") \
} while (0)

// always present and remove notifications, to get growl-like functionality
- (void)userNotificationCenter:(NSUserNotificationCenter *)center
       didActivateNotification:(NSUserNotification *)notification {
    (void)center;
    if (notification.activationType == NSUserNotificationActivationTypeContentsClicked &&
        notification.userInfo &&
        notification.userInfo[SFX_ACTION_KEY]) {
        SuppressPerformSelectorLeakWarning([self performSelector:NSSelectorFromString(notification.userInfo[SFX_ACTION_KEY])]);
    }
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
     shouldPresentNotification:(NSUserNotification *)notification {
    (void) center;
    (void) notification;
    return YES;
}

- (void)_loadAboutWindow {
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];

    if (!self.aboutWindowController) {
        self.aboutWindowController = [SFXAboutWindowController.alloc initWithWindowNibName:@"SFXAboutWindow"];
        self.aboutWindowController.window.delegate = self;
    }
    
    [self.aboutWindowController showWindow:nil];
}

static
NSURL *
get_default_cookie_url(NSURL *appSupportDir) {
    if (!appSupportDir) {
        NSError *err;
        appSupportDir = [SFXAppDelegate applicationSupportDirectoryError:&err];
        if (!appSupportDir) throw safe::mac::nserror_to_exception("applicationSupportDirectory", err);
    }
    return appSupportDir;
}

static
bool
check_for_cookie_file_at_url(NSString *file_name, NSURL *appSupportDir) {
    appSupportDir = get_default_cookie_url(appSupportDir);
    NSURL *cookieURL = [appSupportDir URLByAppendingPathComponent:file_name];

    // NB: assuming the following operation is quick
    return [NSFileManager.defaultManager fileExistsAtPath:cookieURL.path];
}

static
void
set_cookie_file_at_url(NSString *file_name, NSURL *appSupportDir, bool set) {
    appSupportDir = get_default_cookie_url(appSupportDir);

    NSURL *cookieURL = [appSupportDir URLByAppendingPathComponent:file_name];

    if (set) {
        // NB: we don't check for error here
        [NSFileManager.defaultManager createFileAtPath:cookieURL.path
                                              contents:NSData.data
                                            attributes:nil];
    }
    else {
        NSError *err;
        BOOL removed = [NSFileManager.defaultManager
                        removeItemAtURL:cookieURL
                        error:&err];
        if (!removed) {
            throw safe::mac::nserror_to_exception("removeItemAtURL", err);
        }
    }
}

static
void
set_cookie_file_at_url_async(NSString *file_name, NSURL *appSupportDir, bool set,
                             void (^when_done)(std::exception_ptr)) {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       try {
                           set_cookie_file_at_url(file_name, appSupportDir, set);
                           if (when_done) when_done(std::exception_ptr());
                       }
                       catch (const std::exception & err) {
                           // user wasn't interested in errors so we just log it
                           lbx_log_error("error setting cookie file url: %s %s: %d: %s",
                                         file_name.UTF8String, appSupportDir.path.UTF8String,
                                         (int) set, err.what());
                           if (when_done) when_done(std::current_exception());
                       }
                   });
}

// NB: this interface currently throws an exception
//     it should instead return an error
- (BOOL)haveStartedAppBefore:(NSURL *)appSupportDir {
    return check_for_cookie_file_at_url(safe::mac::to_ns_string(SAFE_APP_STARTED_COOKIE_FILENAME),
                                        appSupportDir);
}

- (void)recordAppStart {
    set_cookie_file_at_url_async(safe::mac::to_ns_string(SAFE_APP_STARTED_COOKIE_FILENAME),
                                 nil, true, nil);
}

- (void)startAppUI {
    auto have_run_app_before = [self haveStartedAppBefore:nil];

    if (!have_run_app_before) {
        // initialize state
        self.shouldRememberPassword = YES;
    }

    [self _setupStatusBar];
    [self recordAppStart];

    if (self->path_store && !self->path_store->empty()) {
        [self mountNthMostRecentlyMounted:0];
    }
    else if (have_run_app_before) {
        set_app_to_run_at_login(true);
        
        if ([self haveUserNotifications]) {
            [self notifyUserTitle:safe::mac::to_ns_string(SAFE_TRAY_ICON_WELCOME_TITLE)
                          message:safe::mac::to_ns_string(SAFE_TRAY_ICON_MAC_WELCOME_MSG)
                           action:@selector(clickStatusItem)];
        }
        else [self clickStatusItem];
    }
    else {
        [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
        self.welcomeWindowDelegate = [SFXWelcomeWindowController.alloc
                                      initWithDelegate:self];
        [self.welcomeWindowDelegate showWindow:nil];
    }
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    
    [self.aboutWindowController.window orderOut:self];
    self.aboutWindowController = nil;
    [self restoreLastActive];
}

- (void)computerSleepStateChanged:(NSNotification *)notification {
    (void) notification;
    for (const auto & md : self->mounts) {
        md.disconnect_clients();
    }
}

static
bool
make_required_system_changes_as_admin() {
    AuthorizationRef auth_ref;
    auto status = AuthorizationCreate(nullptr, kAuthorizationEmptyEnvironment,
                                      kAuthorizationFlagDefaults, &auth_ref);
    if (status != errAuthorizationSuccess) throw std::runtime_error("AuthorizationCreate failed");
    auto _free_ref = safe::create_deferred(AuthorizationFree, auth_ref, kAuthorizationFlagDefaults);
    
    AuthorizationItem auth_item = {kAuthorizationRightExecute, 0, nullptr, 0};
    AuthorizationRights rights = {1, &auth_item};
        
    auto flags = (kAuthorizationFlagDefaults |
                  kAuthorizationFlagInteractionAllowed |
                  kAuthorizationFlagPreAuthorize |
                  kAuthorizationFlagExtendRights);
    auto status2 = AuthorizationCopyRights(auth_ref,
                                           &rights, nullptr,
                                           flags, nullptr);
    
    if (status2 != errAuthorizationSuccess) throw std::runtime_error("AuthorizationCopyRights failed");
    
    return safe::mac::make_required_system_changes_common([&] (const char *executable_path,
                                                                  const char *const args[]) {
        // NB: yes AuthorizationExecuteWithPrivileges is deprecated,
        // the recommendation is to create a launch daemon and bless it as admin
        // using SMJobBless. that's a little overkill for now
        // TODO: make helper tool and use SMJobBless
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        auto status3 = AuthorizationExecuteWithPrivileges(auth_ref, executable_path,
                                                          kAuthorizationFlagDefaults,
                                                          (char *const *)args, nullptr);
#pragma clang diagnostic pop
        if (status3 != errAuthorizationSuccess) throw std::runtime_error("execute failed");
    });
}

const char *WAITING_FOR_REBOOT_SHM_NAME = "SAFE_WAITING_FOR_REBOOT";

static
bool
is_reboot_pending() {
    auto fd = shm_open(WAITING_FOR_REBOOT_SHM_NAME, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) return false;
        throw std::system_error(errno, std::generic_category());
    }
    close(fd);
    return true;
}

static
void
set_reboot_pending(bool waiting) {
    if (waiting) {
        auto fd = shm_open(WAITING_FOR_REBOOT_SHM_NAME, O_WRONLY | O_CREAT, 0777);
        if (fd < 0) throw std::system_error(errno, std::generic_category());
        close(fd);
    }
    else {
        int ret = shm_unlink(WAITING_FOR_REBOOT_SHM_NAME);
        if (ret < 0) throw std::system_error(errno, std::generic_category());
    }
}

- (void)rebootComputerResponse:(NSAlert *)alert
                    returnCode:(NSInteger)returnCode
                   contextInfo:(void *)contextInfo {
    (void) contextInfo;
    
    [alert.window orderOut:self];

    set_reboot_pending(true);

    if (returnCode == NSAlertFirstButtonReturn) {
        safe::mac::reboot_machine();
    }

    [NSApp terminate:nil];
}

- (void)runRebootSequence:(NSWindow *)w {
    // show confirmation dialog
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:safe::mac::to_ns_string(SAFE_DIALOG_REBOOT_CONFIRMATION_TITLE)];
    [alert setInformativeText:safe::mac::to_ns_string(SAFE_DIALOG_REBOOT_CONFIRMATION_MESSAGE_MAC)];
    [alert addButtonWithTitle:@"Reboot Now"];
    [alert addButtonWithTitle:@"Quit and Reboot Later"];
    [alert setAlertStyle:NSWarningAlertStyle];
    
    [alert beginSheetModalForWindow:w
                      modalDelegate:self
                     didEndSelector:@selector(rebootComputerResponse:returnCode:contextInfo:)
                        contextInfo:nil];
}
- (void)makeSystemChangesProgressDialog:(NSWindow *)window {
    auto onSuccess = ^(bool reboot_required) {
        if (reboot_required) {
            [self runRebootSequence:window];
        }
        else {
            // delete the system changes dialog
            self.systemChangesWindowController = nil;
            // start app as usual
            [self startAppUI];
        }
    };
    
    // perform changes
    showBlockingSheetMessageNoCatch(window,
                                    safe::mac::to_ns_string(SAFE_PROGRESS_SYSTEM_CHANGES_TITLE),
                                    onSuccess,
                                    make_required_system_changes_as_admin);
}

- (void)cancelSystemChangesResponse:(NSAlert *)alert
                         returnCode:(NSInteger)returnCode
                        contextInfo:(void *)contextInfo {
    NSWindow *window = (__bridge NSWindow *) contextInfo;
    
    if (returnCode == NSAlertFirstButtonReturn) {
        [alert.window orderOut:self];
        // make system changes, chain the sheets
        [self makeSystemChangesProgressDialog:window];
    }
    else if (returnCode == NSAlertSecondButtonReturn) {
        // quit the app
        [NSApp terminate:nil];
    }
    else assert(false);
}

- (void)systemChangesResponseWithController:(SFXWelcomeWindowController *)c action:(welcome_window_action_t)action {
    switch (action) {
        case WELCOME_WINDOW_BUTTON_0: {
            [self makeSystemChangesProgressDialog:c.window];
            return;
        }
        case WELCOME_WINDOW_BUTTON_1: {
            // pop-up more info dialog
            safe::mac::open_url(SAFE_MAC_SYSTEM_CHANGES_INFO_WEBSITE);
            return;
        }
        case WELCOME_WINDOW_NONE: {
            // show confirmation dialog
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:safe::mac::to_ns_string(SAFE_DIALOG_CANCEL_SYSTEM_CHANGES_TITLE)];
            [alert setInformativeText:safe::mac::to_ns_string(SAFE_DIALOG_CANCEL_SYSTEM_CHANGES_MESSAGE)];
            [alert addButtonWithTitle:@"Make Changes"];
            [alert addButtonWithTitle:@"Quit"];
            [alert setAlertStyle:NSWarningAlertStyle];
            
            [alert beginSheetModalForWindow:c.window
                              modalDelegate:self
                             didEndSelector:@selector(cancelSystemChangesResponse:returnCode:contextInfo:)
                                contextInfo:(__bridge void *)c.window];
            return;
        }
    }
    /* notreached */
    assert(false);
}

static
bool
exit_if_not_single_instance(NSURL *app_support_url) {
    NSURL *lock_file_url = [app_support_url
                            URLByAppendingPathComponent:SAFE_MAC_LOCK_FILE];

    auto _fd = open(lock_file_url.path.fileSystemRepresentation, O_CREAT | O_RDONLY, 0777);
    if (_fd < 0) throw std::system_error(errno, std::generic_category());
    auto _close_file = safe::create_deferred(close, _fd);

    auto ret = flock(_fd, LOCK_EX | LOCK_NB);
    if (ret < 0) {
        if (errno == EWOULDBLOCK) {
            return true;
        }
        else throw std::system_error(errno, std::generic_category());
    }

    // intentionally leak file descriptor to retain lock for remainder of process
    // existence
    _close_file.cancel();
    return false;
}

static
void
my_fs_stream_callback(ConstFSEventStreamRef streamRef,
                      void *clientCallBackInfo,
                      size_t numEvents,
                      void *eventPaths,
                      const FSEventStreamEventFlags eventFlags[],
                      const FSEventStreamEventId eventIds[]) {
    (void) streamRef;
    (void) numEvents;
    (void) eventFlags;
    (void) eventIds;
    (void) eventPaths;

    SFXAppDelegate *self = (__bridge SFXAppDelegate *) clientCallBackInfo;

    assert(self->fs_event_stream == streamRef);

    [self handleChangedRememberPassword];
}

- (bool)listenToAppSupportDir:(NSURL *)appSupportDir {
    struct FSEventStreamContext fs_event_stream_ctx = {
        0,
        (__bridge void *) self,
        nullptr, nullptr, nullptr,
    };

    self->fs_event_stream = FSEventStreamCreate(kCFAllocatorDefault,
                                                my_fs_stream_callback,
                                                &fs_event_stream_ctx,
                                                (__bridge CFArrayRef) @[appSupportDir.path],
                                                kFSEventStreamEventIdSinceNow, 0,
                                                kFSEventStreamCreateFlagUseCFTypes |
                                                kFSEventStreamCreateFlagIgnoreSelf);
    if (!self->fs_event_stream) return false;
    auto _free_fs_event_stream = safe::create_deferred([&] {
        FSEventStreamStop(self->fs_event_stream);
        FSEventStreamInvalidate(self->fs_event_stream);
        FSEventStreamRelease(self->fs_event_stream);
    });

    FSEventStreamScheduleWithRunLoop(self->fs_event_stream,
                                     CFRunLoopGetCurrent(),
                                     kCFRunLoopDefaultMode);

    auto success = FSEventStreamStart(self->fs_event_stream);
    if (!success) return false;

    _free_fs_event_stream.cancel();

    return true;
}

[[noreturn]]
static
void
my_terminate_handler() {
    auto current_exc = std::current_exception();

    auto main_block_to_run = ^{
        auto error_message = ("An unexpected error occurred. Please help us improve by sending a bug report. It's automatic and "
                              "no personal information is used.");

        [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Unexpected Error Occurred"];
        [alert setInformativeText:safe::mac::to_ns_string(error_message)];
        [alert addButtonWithTitle:@"Report Bug"];
        [alert addButtonWithTitle:@"Quit"];
        [alert setAlertStyle:NSWarningAlertStyle];

        auto response = [alert runModal];
        if (response == NSAlertFirstButtonReturn) {
            safe::report_exception(safe::ExceptionLocation::UNEXPECTED, current_exc);
        }
    };

    if (dispatch_get_current_queue() == dispatch_get_main_queue()) {
        main_block_to_run();
    }
    else {
        dispatch_sync(dispatch_get_main_queue(), main_block_to_run);
    }

    std::_Exit(-1);
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    (void)aNotification;
    std::set_terminate(my_terminate_handler);

    log_printer_default_init();
    encfs_set_log_printer(encfs_log_printer_adapter);

#ifdef NDEBUG
    logging_set_global_level(LOG_NOTHING);
    encfs_set_log_level(ENCFS_LOG_NOTHING);
#else
    logging_set_global_level(LOG_DEBUG);
    encfs_set_log_level(ENCFS_LOG_DEBUG);
#endif
    log_debug("Hello world!");
    
    NSError *err;
    NSURL *appSupportDir = [SFXAppDelegate applicationSupportDirectoryError:&err];
    if (!appSupportDir) {
        throw std::runtime_error(safe::mac::from_ns_string(err.localizedDescription));
    }

    // check if app is already running
    if (exit_if_not_single_instance(appSupportDir)) {
        // TODO: would be nice to trigger default action in currently running app
        //       just like on windows
        [NSApp terminate:nil];
        return;
    }
    
    safe::global_webdav_init();
    
    self->native_fs = safe::create_native_fs();
    
    auto recently_used_paths_storage_path =
    safe::mac::url_to_path(self->native_fs, appSupportDir).join(SAFE_RECENTLY_USED_PATHS_DB_FILE_NAME);
    
    try {
        self->path_store = safe::make_unique<safe::mac::RecentlyUsedNSURLStoreV1>(self->native_fs, recently_used_paths_storage_path, SAFE_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);
    }
    catch (const safe::RecentlyUsedPathsParseError & err) {
        lbx_log_error("Parse error on recently used path store: %s", err.what());
        // delete path and try again
        self->native_fs->unlink(recently_used_paths_storage_path);
        self->path_store = safe::make_unique<safe::mac::RecentlyUsedNSURLStoreV1>(self->native_fs, recently_used_paths_storage_path, SAFE_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);
    }

    self.createWindows = [NSMutableArray array];
    self.mountWindows = [NSMutableArray array];

    auto success = [self listenToAppSupportDir:appSupportDir];
    if (!success) throw std::runtime_error("failed to watch app support dir!");

    // TODO: consider releasing when app dies self->fs_events_stream

    // we do this to make sure this setting points to the last run app
    normalize_app_is_run_at_login_setting();

    // get notification whenever active application changes,
    // we preserve this so we can switch back when the user
    // selects "cancel" on our dialogs
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                                                           selector:@selector(saveCurrentlyActive:)
                                                               name:NSWorkspaceDidActivateApplicationNotification
                                                             object:nil];
    // kickoff mount watch timer
    // get notifaction whenever device is unmounted
    // so we can update our internal structures
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                                                           selector:@selector(driveWasUnmounted:)
                                                               name:NSWorkspaceDidUnmountNotification
                                                             object:nil];
    
    // receive events when computer wakes up to disconnect all connected clients
    // (this is a workaround to a bug in the mac os x webdav client, where it would
    //  sometimes hang on a connection that was open before sleep)
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                                                           selector:@selector(computerSleepStateChanged:)
                                                               name:NSWorkspaceWillSleepNotification
                                                             object:NULL];
    
//    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
//                                                           selector:@selector(computerSleepStateChanged:)
//                                                               name:NSWorkspaceDidWakeNotification
//                                                             object:nil];

    if ([self haveUserNotifications]) {
        NSUserNotificationCenter.defaultUserNotificationCenter.delegate = self;
    }

    if (is_reboot_pending()) {
        [self runRebootSequence:nil];
    } else if (safe::mac::system_changes_are_required()) {
        [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
        decltype(self) __weak weakSelf = self;
        self.systemChangesWindowController =
        [SFXWelcomeWindowController.alloc
         initWithBlock:^(SFXWelcomeWindowController *c, welcome_window_action_t action) {
             [weakSelf systemChangesResponseWithController:c action:action];
         }
         title:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_TITLE)
         message:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_MESSAGE)
         button0Title:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_OK)
         button1Title:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_MORE_INFO)
         ];
    }
    else [self startAppUI];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSNotification *)notification {
    (void)notification;
    
    for (auto it = self->mounts.begin(); it != self->mounts.end();) {
        try {
            it = [self unmountAndStopDrive:it withUI:NO];
        }
        catch (const std::exception & err) {
            [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
            NSAlert *alert =
                [NSAlert alertWithMessageText:@"Unable to Unmount"
                                defaultButton:nil
                              alternateButton:nil
                                  otherButton:nil
	                informativeTextWithFormat:(@"Safe could not quit because it could not unmount \"%s.\" It's most likely in use. "
                                               @"Please close all applications using files in \"%s\" before attempting to quit again."),
                 it->get_mount_name().c_str(), it->get_mount_name().c_str()
                 ];
            [alert beginSheetModalForWindow:nil modalDelegate:nil didEndSelector:nil contextInfo:nil];
            return NSTerminateCancel;
        }
    }
    
    safe::global_webdav_shutdown();
    return NSTerminateNow;
}

- (void)driveWasUnmounted:(NSNotification *)p {
    (void)p;

    // check all devices and if any of them were no longer unmount
    // then stop and update status
    // TODO: maybe just unmount them based on the notification
    //       are notifications reliable? guessing yes
    for (auto it = self->mounts.begin(); it != self->mounts.end();) {
        if (!it->is_still_mounted()) {
            it = [self stopDrive:it withUI:YES];
        }
        else ++it;
    }
}

- (void)handleChangedRememberPassword {
    for (SFXCreateSafeWindowController *c in self.createWindows) {
        [c shouldRememberPasswordChanged];
    }
    for (SFXMountSafeWindowController *c in self.mountWindows) {
        [c shouldRememberPasswordChanged];
    }
}

- (BOOL)shouldRememberPassword {
    return check_for_cookie_file_at_url
    (safe::mac::to_ns_string(SAFE_REMEMBER_PASSWORD_COOKIE_FILENAME),
     nil);
}

- (void)setShouldRememberPassword:(BOOL)shouldRememberPasword {
    set_cookie_file_at_url_async(safe::mac::to_ns_string(SAFE_REMEMBER_PASSWORD_COOKIE_FILENAME),
                                 nil, shouldRememberPasword,
                                 ^(std::exception_ptr) {
                                     [self handleChangedRememberPassword];
                                 });
}

@end

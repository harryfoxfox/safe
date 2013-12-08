//
//  LBXAppDelegate.mm
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/LBXAppDelegate.h>

#import <lockbox/mount_mac.hpp>
#import <lockbox/lockbox_constants.h>
#import <lockbox/lockbox_server.hpp>
#import <lockbox/logging.h>
#import <lockbox/recent_paths_storage.hpp>
#import <lockbox/tray_menu.hpp>
#import <lockbox/util.hpp>

// 10 to model after system mac recent menus
static NSString *const LBX_ACTION_KEY = @"_lbx_action";

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
    set_property(lockbox::TrayMenuProperty name, std::string value) {
        // we don't support menu tooltips
        if (name == lockbox::TrayMenuProperty::MAC_FILE_TYPE) {
            _menu_item.image = [NSWorkspace.sharedWorkspace
                                iconForFileType:[NSString stringWithUTF8String:value.c_str()]];
            if (!_menu_item.image) throw std::runtime_error("bad icon name");
            _menu_item.image.size = NSMakeSize(16, 16);
            return true;
        }
        return false;
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
                lockbox::TrayMenuAction action,
                lockbox::tray_menu_action_arg_t action_arg = 0) {
        NSMenuItem *mi = [NSMenuItem.alloc initWithTitle:[NSString stringWithUTF8String:title.c_str()]
                                                  action:_sel
                                           keyEquivalent:@""];
        mi.target = _target;
        mi.tag = lockbox::encode_menu_id<NSInteger>(action, action_arg);
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
                                                  action:_sel
                                           keyEquivalent:@""];
        mi.submenu = recentSubmenu;
        [_menu addItem:mi];
        return MacOSXTrayMenu(recentSubmenu, _target, _sel);
    }
};

@implementation LBXAppDelegate

- (opt::optional<lockbox::mac::MountDetails>)takeMount:(const encfs::Path &)p {
    auto it = std::find_if(self->mounts.begin(), self->mounts.end(),
                           [&] (const lockbox::mac::MountDetails & md) {
                               return md.get_source_path() == p;
                           });
    if (it == self->mounts.end()) return opt::nullopt;

    auto md = std::move(*it);
    self->mounts.erase(it);
    return std::move(md);
}

- (NSURL *)applicationSupportDirectoryError:(NSError **)err {
    NSURL *p = [NSFileManager.defaultManager
                URLForDirectory:NSApplicationSupportDirectory
                inDomain:NSUserDomainMask
                appropriateForURL:nil
                create:YES error:err];
    if (!p) return nil;
    
    NSString *executableName = NSBundle.mainBundle.infoDictionary[@"CFBundleExecutable"];
    
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
    if (!self.createWindows.count && !self.mountWindows.count) {
        [self.lastActiveApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    }
}

- (void)_newMount:(lockbox::mac::MountDetails)md {
    self->path_store->use_path(md.get_source_path());
    self->mounts.insert(self->mounts.begin(), std::move(md));
    const auto & new_mount = self->mounts.front();
    [self _updateStatusMenu];
    new_mount.open_mount();
    [self notifyUserTitle:@"Success"
                  message:[NSString stringWithFormat:@"You've successfully mounted \"%s.\"",
                           new_mount.get_mount_name().c_str(), nil]];
}

- (void)createLockboxDone:(LBXCreateLockboxWindowController *)wc
                    mount:(opt::optional<lockbox::mac::MountDetails>)maybeMount {
    [self.createWindows removeObject:wc];
    if (maybeMount) [self _newMount:std::move(*maybeMount)];
    else [self restoreLastActive];
}

- (void)mountLockboxDone:(LBXMountLockboxWindowController *)wc
                   mount:(opt::optional<lockbox::mac::MountDetails>)maybeMount {
    [self.mountWindows removeObject:wc];
    if (maybeMount) [self _newMount:std::move(*maybeMount)];
    else [self restoreLastActive];
}

- (void)openMountDialogForPath:(encfs::Path)p {
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
    LBXMountLockboxWindowController *wc = [LBXMountLockboxWindowController.alloc
                                           initWithDelegate:self
                                           fs:self->native_fs
                                           path:std::move(p)];
    
    [self.mountWindows addObject:wc];
}

- (void)_dispatchMenu:(id)sender {
    using lockbox::TrayMenuAction;

    NSMenuItem *mi = sender;
    
    TrayMenuAction menu_action;
    lockbox::tray_menu_action_arg_t menu_action_arg;
    
    std::tie(menu_action, menu_action_arg) = lockbox::decode_menu_id(mi.tag);
    
    switch (menu_action) {
        case TrayMenuAction::CREATE: {
            [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
            LBXCreateLockboxWindowController *wc = [[LBXCreateLockboxWindowController alloc]
                                                    initWithDelegate:self fs:self->native_fs];
            [self.createWindows addObject:wc];
            break;
        }
        case TrayMenuAction::MOUNT: {
            [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
            LBXMountLockboxWindowController *wc = [[LBXMountLockboxWindowController alloc]
                                                   initWithDelegate:self fs:self->native_fs];
            [self.mountWindows addObject:wc];
            break;
        }
        case TrayMenuAction::ABOUT_APP: {
            [self _loadAboutWindowTitle:[NSString stringWithUTF8String:LOCKBOX_DIALOG_ABOUT_TITLE]];
            break;
        }
        case TrayMenuAction::TRIGGER_BREAKPOINT: {
            Debugger();
            break;
        }
        case TrayMenuAction::TEST_BUBBLE: {
            [self notifyUserTitle:[NSString stringWithUTF8String:LOCKBOX_NOTIFICATION_TEST_TITLE]
                          message:[NSString stringWithUTF8String:LOCKBOX_NOTIFICATION_TEST_MESSAGE]];
            break;
        }
        case TrayMenuAction::QUIT_APP: {
            bool actually_quit = true;
            if (!self->mounts.empty()) {
                [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
                NSAlert *alert = [NSAlert alertWithMessageText:[NSString stringWithUTF8String:LOCKBOX_DIALOG_QUIT_CONFIRMATION_TITLE]
                                                 defaultButton:@"Quit"
                                               alternateButton:@"Cancel"
                                                   otherButton:nil
                                     informativeTextWithFormat:[NSString stringWithUTF8String:LOCKBOX_DIALOG_QUIT_CONFIRMATION_MESSAGE], nil];
                actually_quit = [alert runModal] == NSAlertDefaultReturn;
            }
            if (actually_quit) [NSApp terminate:sender];
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
            self->mounts[menu_action_arg].unmount();
            break;
        }
        case TrayMenuAction::CLEAR_RECENTS: {
            self->path_store->clear();
            [self _updateStatusMenu];
            break;
        }
        case TrayMenuAction::MOUNT_RECENT: {
            const auto & recent_paths = self->path_store->recently_used_paths();
            if (menu_action_arg >= recent_paths.size()) return;
            [self openMountDialogForPath:recent_paths[menu_action_arg]];
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

- (void)_updateStatusMenu {
    NSMenu *menu = self.statusItem.menu;
    
    [menu removeAllItems];
    
    bool show_alternative_menu = self->lastModifierFlags & NSAlternateKeyMask;
    auto m = MacOSXTrayMenu(menu, self, @selector(_dispatchMenu:));
    lockbox::populate_tray_menu(m, self->mounts, *self->path_store, show_alternative_menu);
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
    
    NSString *executableName = NSBundle.mainBundle.infoDictionary[@"CFBundleExecutable"];

    self.statusItem.title = executableName;
    self.statusItem.highlightMode = YES;
    self.statusItem.menu = statusMenu;
    self.statusItem.toolTip = [NSString stringWithUTF8String:LOCKBOX_TRAY_ICON_TOOLTIP];
    
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
        user_not.userInfo = @{LBX_ACTION_KEY: NSStringFromSelector(sel)};
    }

    [NSUserNotificationCenter.defaultUserNotificationCenter
     scheduleNotification:user_not];
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
        notification.userInfo[LBX_ACTION_KEY]) {
        SuppressPerformSelectorLeakWarning([self performSelector:NSSelectorFromString(notification.userInfo[LBX_ACTION_KEY])]);
    }
}

- (void)userNotificationCenter:(NSUserNotificationCenter *)center
        didDeliverNotification:(NSUserNotification *)notification {
    [center removeDeliveredNotification:notification];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
     shouldPresentNotification:(NSUserNotification *)notification {
    (void) center;
    (void) notification;
    return YES;
}

- (void)_loadAboutWindowTitle:(NSString *)title {
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];

    if (!self.aboutWindow) {
        [NSBundle loadNibNamed:@"LBXAboutWindow" owner:self];
        self.aboutWindow.delegate = self;
        self.aboutWindow.level = NSModalPanelWindowLevel;
        self.aboutWindowText.stringValue = [NSString stringWithUTF8String:LOCKBOX_ABOUT_BLURB];
        self.aboutWindow.title = title;
    }
    
    [self.aboutWindow makeKeyAndOrderFront:self];
}

- (IBAction)aboutWindowOK:(NSButton *)sender {
    [sender.window performClose:sender];
}

- (IBAction)aboutWindowGetSourceCode:(NSButton *)sender {
    (void) sender;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:LOCKBOX_SOURCE_CODE_WEBSITE]]];
}

- (BOOL)haveStartedAppBefore:(NSURL *)appSupportDir {
    NSURL *cookieURL = [appSupportDir URLByAppendingPathComponent:[NSString stringWithUTF8String:LOCKBOX_APP_STARTED_COOKIE_FILENAME]];
    // NB: assuming the following operation is quick
    return [NSFileManager.defaultManager fileExistsAtPath:cookieURL.path];
}

- (void)recordAppStart {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       NSError *err;
                       NSURL *appSupportDir = [self applicationSupportDirectoryError:&err];
                       if (!appSupportDir) {
                           lbx_log_critical("Error while getting application support directory: (%s:%ld) %s",
                                            err.domain.UTF8String,
                                            (long) err.code,
                                            err.localizedDescription.UTF8String);
                           return;
                       }
                       
                       NSURL *cookieURL = [appSupportDir URLByAppendingPathComponent:[NSString stringWithUTF8String:LOCKBOX_APP_STARTED_COOKIE_FILENAME]];
                       [NSFileManager.defaultManager createFileAtPath:cookieURL.path
                                                             contents:NSData.data
                                                           attributes:nil];
                   });
}

- (void)startAppUI {
    [self _setupStatusBar];
    [self recordAppStart];
    
    if ([self haveUserNotifications]) {
        [self notifyUserTitle:[NSString stringWithUTF8String:LOCKBOX_TRAY_ICON_WELCOME_TITLE]
                      message:[NSString stringWithUTF8String:LOCKBOX_TRAY_ICON_MAC_WELCOME_MSG]
                       action:@selector(clickStatusItem)];
    }
    else [self clickStatusItem];
}

- (void)windowWillClose:(NSNotification *)notification {
    (void) notification;
    
    [self.aboutWindow orderOut:self];
    self.aboutWindow = nil;
    [self restoreLastActive];

    // only do the following if this is first launch (i.e. self.statusItem has not been set)
    if (self.statusItem) return;
    
    [self startAppUI];
}

- (void)computerSleepStateChanged:(NSNotification *)notification {
    (void) notification;
    for (const auto & md : self->mounts) {
        md.disconnect_clients();
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    (void)aNotification;
    
    log_printer_default_init();
    logging_set_global_level(LOG_DEBUG);
    log_debug("Hello world!");
    
    NSError *err;
    NSURL *appSupportDir = [self applicationSupportDirectoryError:&err];
    if (!appSupportDir) {
        lbx_log_critical("Error while getting application support directory: (%s:%ld) %s",
                         err.domain.UTF8String,
                         (long) err.code,
                         err.localizedDescription.UTF8String);
        [NSApp presentError:err];
        [NSApp terminate:self];
        return;
    }
    
    lockbox::global_webdav_init();
    
    self->native_fs = lockbox::create_native_fs();
    
    auto recently_used_paths_storage_path =
    self->native_fs->pathFromString(appSupportDir.path.fileSystemRepresentation).join(LOCKBOX_RECENTLY_USED_PATHS_V1_FILE_NAME);
    
    try {
        try {
            self->path_store = lockbox::make_unique<lockbox::RecentlyUsedPathStoreV1>(self->native_fs, recently_used_paths_storage_path, LOCKBOX_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);
        }
        catch (const lockbox::RecentlyUsedPathsParseError & err) {
            lbx_log_error("Parse error on recently used path store: %s", err.what());
            // delete path and try again
            self->native_fs->unlink(recently_used_paths_storage_path);
            self->path_store = lockbox::make_unique<lockbox::RecentlyUsedPathStoreV1>(self->native_fs, recently_used_paths_storage_path, LOCKBOX_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);
        }
    }
    catch (const std::exception & err) {
        // error while creating recently used path storee
        // not a huge deal, but log error
        lbx_log_error("Couldn't create recently used path store: %s", err.what());
    }
    
    self.createWindows = [NSMutableArray array];
    self.mountWindows = [NSMutableArray array];
    
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

    if (self->path_store && !self->path_store->recently_used_paths().empty()) {
        [self _setupStatusBar];
        [self openMountDialogForPath:self->path_store->recently_used_paths()[0]];
    }
    else if ([self haveStartedAppBefore:appSupportDir]) {
        [self startAppUI];
    }
    else {
        [self _loadAboutWindowTitle:[NSString stringWithUTF8String:LOCKBOX_DIALOG_WELCOME_TITLE]];
    }
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    
    for (auto & md : self->mounts) {
        md.unmount();
    }
    
    lockbox::global_webdav_shutdown();
}

- (void)driveWasUnmounted:(NSNotification *)p {
    (void)p;

    // check all devices and if any of them were no longer unmount
    // then stop and update status
    // TODO: maybe just unmount them based on the notification
    //       are notifications reliable? guessing yes
    for (auto it = self->mounts.begin(); it != self->mounts.end();) {
        if (!it->is_still_mounted()) {
            it->signal_stop();
            auto mount_name = it->get_mount_name();
            it = self->mounts.erase(it);
            [self _updateStatusMenu];
            [self notifyUserTitle:@"Success"
                          message:[NSString stringWithFormat:@"You've successfully unmounted \"%s.\"",
                                   mount_name.c_str(), nil]];
        }
        else ++it;
    }
}

@end

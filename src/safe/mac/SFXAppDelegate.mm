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
#import <safe/recent_paths_storage.hpp>
#import <safe/mac/system_changes.hpp>
#import <safe/tray_menu.hpp>
#import <safe/util.hpp>
#import <safe/mac/util.hpp>
#import <safe/webdav_server.hpp>

// 10 to model after system mac recent menus
static NSString *const SFX_ACTION_KEY = @"_lbx_action";

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

const auto LS_SHARED_FILE_LIST_STARTUP_ITEM_PROPERTY = @"SafeStartupItem";

template<class F>
static
void
iterate_shared_file_list(LSSharedFileListRef list_ref, F f) {
    // iterate through every item to find out if we're run at login
    UInt32 snapshot_seed;
    auto item_array_ref = LSSharedFileListCopySnapshot(list_ref, &snapshot_seed);
    if (!item_array_ref) throw std::runtime_error("couldn't create snapshot array");
    auto _release_item_array = safe::create_deferred(CFRelease, item_array_ref);
    
    auto array_size = CFArrayGetCount(item_array_ref);
    for (CFIndex i = 0; i < array_size; ++i) {
        auto item_ref = (LSSharedFileListItemRef) CFArrayGetValueAtIndex(item_array_ref, i);
        auto continue_ = f(item_ref);
        if (!continue_) break;
    }
}

static
bool
list_item_equals_nsurl(LSSharedFileListItemRef item_ref, NSURL *url) {
    CFURLRef out_url;
    auto err = LSSharedFileListItemResolve(item_ref,
                                           kLSSharedFileListNoUserInteraction,
                                           &out_url, nullptr);
    if (err != noErr) {
        // not sure under what circumstances this can happen but let's not fail on this
        lbx_log_error("Error while calling LSSharedFileListItemResolve: 0x%x", (unsigned) err);
        return false;
    }
    
    auto _free_url = safe::create_deferred(CFRelease, out_url);
    
    return [(__bridge NSURL *) out_url isEqual:url];
}

static
LSSharedFileListItemRef
copy_safe_startup_item_ref(LSSharedFileListRef list_ref) {
    LSSharedFileListItemRef toret = nullptr;
    
    // iterate through every item to find out if we're run at login
    iterate_shared_file_list(list_ref, [&](LSSharedFileListItemRef item_ref) {
        // does item have our special tag and point to this app?
        auto startup_property_value = LSSharedFileListItemCopyProperty(item_ref, (__bridge CFStringRef) LS_SHARED_FILE_LIST_STARTUP_ITEM_PROPERTY);
        if (!startup_property_value) return true;
        auto _free_property_value = safe::create_deferred(CFRelease, startup_property_value);
        
        // if not a string type, doesn't match
        if (CFGetTypeID(startup_property_value) != CFStringGetTypeID()) return true;
        
        // if not equal to the key, doesn't match
        if (![(__bridge NSString *) startup_property_value isEqualToString:LS_SHARED_FILE_LIST_STARTUP_ITEM_PROPERTY]) return true;
        
        CFRetain(item_ref);
        toret = item_ref;
        
        return false;
    });
    
    return toret;
}

static
bool
app_is_run_at_login() {
	auto appPath = NSBundle.mainBundle.bundlePath;
    NSURL *appURL = [NSURL fileURLWithPath:appPath];
    
    auto login_items_ref =
        LSSharedFileListCreate(nullptr,
                               kLSSharedFileListSessionLoginItems, nullptr);
    if (!login_items_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, login_items_ref);
    
    auto item_ref = copy_safe_startup_item_ref(login_items_ref);
    if (!item_ref) return false;
    auto _free_item_ref = safe::create_deferred(CFRelease, item_ref);

    // does path resolve to our executable?
    return list_item_equals_nsurl(item_ref, appURL);
}

static
void
set_app_to_run_at_login(bool run_at_login) {
    auto appPath = NSBundle.mainBundle.bundlePath;
    NSURL *appURL = [NSURL fileURLWithPath:appPath];
    
    auto login_items_ref =
    LSSharedFileListCreate(nullptr,
                           kLSSharedFileListSessionLoginItems, nullptr);
    if (!login_items_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, login_items_ref);
    
    // remove old item if it exists
    auto item_ref = copy_safe_startup_item_ref(login_items_ref);
    if (item_ref) {
        auto _free_item_ref = safe::create_deferred(CFRelease, item_ref);
        auto ret = LSSharedFileListItemRemove(login_items_ref, item_ref);
        if (ret != noErr) throw std::runtime_error("error removing old item");
        item_ref = nullptr;
    }
    
    if (run_at_login) {
        // create item
        auto new_item_ref = LSSharedFileListInsertItemURL(login_items_ref,
                                                          kLSSharedFileListItemLast,
                                                          (__bridge CFStringRef) safe::mac::to_ns_string(PRODUCT_NAME_A),
                                                          nullptr,
                                                          (__bridge CFURLRef) appURL,
                                                          (__bridge CFDictionaryRef) @{LS_SHARED_FILE_LIST_STARTUP_ITEM_PROPERTY :
                                                                                           LS_SHARED_FILE_LIST_STARTUP_ITEM_PROPERTY},
                                                          nullptr);
        if (!new_item_ref) throw std::runtime_error("item creation failed");
        auto _free_item_ref = safe::create_deferred(CFRelease, new_item_ref);
    }
}

static
void
add_mount_to_favorites(const safe::mac::MountDetails & mount) {
    auto list_ref =
    LSSharedFileListCreate(nullptr,
                           kLSSharedFileListFavoriteVolumes, nullptr);
    if (!list_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, list_ref);

    NSURL *mount_url = [NSURL fileURLWithPath:safe::mac::to_ns_string(mount.get_mount_point())];
    
    // create item
    auto new_item_ref = LSSharedFileListInsertItemURL(list_ref,
                                                      kLSSharedFileListItemBeforeFirst,
                                                      (__bridge CFStringRef) safe::mac::to_ns_string(mount.get_mount_name() +
                                                                                                        " (" PRODUCT_NAME_A " Mount)"),
                                                      nullptr,
                                                      (__bridge CFURLRef) mount_url,
                                                      nullptr,
                                                      nullptr);
    if (!new_item_ref) throw std::runtime_error("item creation failed");
    auto _free_item_ref = safe::create_deferred(CFRelease, new_item_ref);
}

static
void
remove_mount_from_favorites(const safe::mac::MountDetails & mount) {
    (void) mount;
    auto list_ref =
    LSSharedFileListCreate(nullptr,
                           kLSSharedFileListFavoriteVolumes, nullptr);
    if (!list_ref) throw std::runtime_error("unable to create shared file list");
    auto _release_login_items = safe::create_deferred(CFRelease, list_ref);

    NSURL *mount_url = [NSURL fileURLWithPath:safe::mac::to_ns_string(mount.get_mount_point())];
    
    iterate_shared_file_list(list_ref, [&](LSSharedFileListItemRef item_ref) {
        if (list_item_equals_nsurl(item_ref, mount_url)) {
            auto ret = LSSharedFileListItemRemove(list_ref, item_ref);
            if (ret != noErr) throw std::runtime_error("error deleting item from list");
            return false;
        }

        return true;
    });
}

@implementation SFXAppDelegate

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
    if (!self.createWindows.count && !self.mountWindows.count && !self.welcomeWindowDelegate &&
        (!self.aboutWindowController || !self.aboutWindowController.window.isVisible)) {
        [self.lastActiveApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    }
}

- (std::vector<safe::mac::MountDetails>::iterator)stopDrive:(std::vector<safe::mac::MountDetails>::iterator)it
                                                        withUI:(BOOL)ui {
    auto mount_name = it->get_mount_name();

    remove_mount_from_favorites(*it);
    it->signal_stop();
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
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
    SFXMountSafeWindowController *wc = [SFXMountSafeWindowController.alloc
                                           initWithDelegate:self
                                           fs:self->native_fs
                                           path:std::move(maybePath)];
    [self.mountWindows addObject:wc];
    [wc showWindow:nil];
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

- (void)_dispatchMenu:(id)sender {
    using safe::TrayMenuAction;

    NSMenuItem *mi = sender;
    
    TrayMenuAction menu_action;
    safe::tray_menu_action_arg_t menu_action_arg;
    
    std::tie(menu_action, menu_action_arg) = safe::decode_menu_id(mi.tag);
    
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
            [self unmountAndStopDrive:self->mounts.begin() + menu_action_arg withUI:YES];
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
        case TrayMenuAction::SEND_FEEDBACK: {
            [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:SAFE_SEND_FEEDBACK_WEBSITE]]];
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
    auto startup_item_is_enabled = app_is_run_at_login();
    safe::populate_tray_menu(m, self->mounts, *self->path_store, startup_item_is_enabled, show_alternative_menu);
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
        notification.userInfo[SFX_ACTION_KEY]) {
        SuppressPerformSelectorLeakWarning([self performSelector:NSSelectorFromString(notification.userInfo[SFX_ACTION_KEY])]);
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

- (void)_loadAboutWindow {
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];

    if (!self.aboutWindowController) {
        self.aboutWindowController = [SFXAboutWindowController.alloc initWithWindowNibName:@"SFXAboutWindow"];
        self.aboutWindowController.window.delegate = self;
    }
    
    [self.aboutWindowController showWindow:nil];
}

- (BOOL)haveStartedAppBefore:(NSURL *)appSupportDir {
    if (!appSupportDir) {
        appSupportDir = [self applicationSupportDirectoryError:nil];
        // TODO: don't do this
        if (!appSupportDir) abort();
    }
    NSURL *cookieURL = [appSupportDir URLByAppendingPathComponent:[NSString stringWithUTF8String:SAFE_APP_STARTED_COOKIE_FILENAME]];
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
                       
                       NSURL *cookieURL = [appSupportDir URLByAppendingPathComponent:[NSString stringWithUTF8String:SAFE_APP_STARTED_COOKIE_FILENAME]];
                       [NSFileManager.defaultManager createFileAtPath:cookieURL.path
                                                             contents:NSData.data
                                                           attributes:nil];
                   });
}

- (void)startAppUI {
    [self _setupStatusBar];
    [self recordAppStart];

    if (self->path_store && !self->path_store->recently_used_paths().empty()) {
        [self openMountDialogForPath:self->path_store->recently_used_paths()[0]];
    }
    else if ([self haveStartedAppBefore:nil]) {
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

- (void)rebootComputerResponse:(NSAlert *)alert
                    returnCode:(NSInteger)returnCode
                   contextInfo:(void *)contextInfo {
    (void) contextInfo;
    
    [alert.window orderOut:self];
    
    if (returnCode == NSAlertFirstButtonReturn) {
        safe::mac::reboot_machine();
    }
    
    [NSApp terminate:nil];
}

- (void)runRebootSequence:(NSWindow *)w {
    // TODO: show reboot confirmation dialog;
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
    
    auto onFail = ^(const std::exception_ptr & eptr) {
        // TODO: potentially give user information to help us debug their problem
        inputErrorAlert(window,
                        @"Couldn't Make System Changes",
                        (@"An error occured while attempting to make changes to your system to increase privacy. "
                         @"Trying again may help. If that doesn't work, please send us a bug report!"));
        try {
            std::rethrow_exception(eptr);
        }
        catch (const std::exception & err) {
            lbx_log_debug("Error while making system changes: %s", err.what());
        }
    };
    
    // perform changes
    showBlockingSheetMessage(window,
                             safe::mac::to_ns_string(SAFE_PROGRESS_SYSTEM_CHANGES_TITLE),
                             onSuccess,
                             onFail,
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

- (bool)systemChangesResponseWithController:(SFXWelcomeWindowController *)c action:(welcome_window_action_t)action {
    switch (action) {
        case WELCOME_WINDOW_BUTTON_0: {
            [self makeSystemChangesProgressDialog:c.window];
            return false;
        }
        case WELCOME_WINDOW_BUTTON_1: {
            // pop-up more info dialog
            safe::mac::open_url(SAFE_WINDOWS_SYSTEM_CHANGES_INFO_WEBSITE);
            return false;
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
            
            return false;
        }
    }
    /* notreached */
    assert(false);
    return true;
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
    
    safe::global_webdav_init();
    
    self->native_fs = safe::create_native_fs();
    
    auto recently_used_paths_storage_path =
    self->native_fs->pathFromString(appSupportDir.path.fileSystemRepresentation).join(SAFE_RECENTLY_USED_PATHS_V1_FILE_NAME);
    
    try {
        try {
            self->path_store = safe::make_unique<safe::RecentlyUsedPathStoreV1>(self->native_fs, recently_used_paths_storage_path, SAFE_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);
        }
        catch (const safe::RecentlyUsedPathsParseError & err) {
            lbx_log_error("Parse error on recently used path store: %s", err.what());
            // delete path and try again
            self->native_fs->unlink(recently_used_paths_storage_path);
            self->path_store = safe::make_unique<safe::RecentlyUsedPathStoreV1>(self->native_fs, recently_used_paths_storage_path, SAFE_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);
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

    if (safe::mac::system_changes_are_required()) {
        decltype(self) __weak weakSelf = self;
        self.systemChangesWindowController =
        [SFXWelcomeWindowController.alloc
         initWithBlock:^(SFXWelcomeWindowController *c, welcome_window_action_t action) {
             return [weakSelf systemChangesResponseWithController:c action:action];
         }
         title:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_TITLE)
         message:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_MESSAGE)
         button0Title:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_OK)
         button1Title:safe::mac::to_ns_string(SAFE_DIALOG_SYSTEM_CHANGES_MORE_INFO)
         ];
    }
    else [self startAppUI];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    
    for (auto it = self->mounts.begin(); it != self->mounts.end();) {
        it = [self unmountAndStopDrive:it withUI:NO];
    }
    
    safe::global_webdav_shutdown();
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


@end

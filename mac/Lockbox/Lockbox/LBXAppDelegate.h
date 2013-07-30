//
//  LBXAppDelegate.h
//  Lockbox
//
//  Created by Rian Hunter on 7/25/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface LBXAppDelegate : NSObject <NSApplicationDelegate>

@property (assign) IBOutlet NSWindow *window;
@property (assign) IBOutlet NSPathControl *srcPathControl;
@property (assign) IBOutlet NSPathControl *dstPathControl;
@property (assign) IBOutlet NSSecureTextField *passwordTextField;
@property (assign) IBOutlet NSPanel *sheetPanel;
@property (assign) IBOutlet NSProgressIndicator *mountProgressIndicator;

- (IBAction)mountEncryptedFS:(id)sender;
- (void)onMountDone:(id)args;

@end

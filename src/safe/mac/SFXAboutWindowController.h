//
//  SFXAboutWindowController.h
//  Safe
//
//  Created by Rian Hunter on 1/4/14.
//  Copyright (c) 2014 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface SFXAboutWindowController : NSWindowController
- (IBAction)getSourceCode:(id)sender;
- (IBAction)visitWebsite:(id)sender;
@property (weak) IBOutlet NSTextField *versionTextField;

@end

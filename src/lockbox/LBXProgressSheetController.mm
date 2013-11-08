//
//  LBXProgressSheetController.m
//  Lockbox
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import "LBXProgressSheetController.h"

@interface LBXProgressSheetController ()

@end

@implementation LBXProgressSheetController

- (id)initWithWindow:(NSWindow *)window
{
    self = [super initWithWindow:window];
    if (self) {
        // Initialization code here.
    }
    return self;
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    
    // Implement this method to handle any initialization after your window controller's window has been loaded from its nib file.
}

- (void)didEndSheet:(NSWindow *)sheet returnCode:(NSInteger)returnCode contextInfo:(void *)contextInfo {
    (void)returnCode;
    (void)contextInfo;
    [sheet orderOut:self];
}

@end

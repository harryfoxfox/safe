/*
 Safe: Encrypted File System
 Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#import <safe/mac/util.hpp>

#import <safe/report_exception.hpp>

#import <exception>

#import <objc/runtime.h>

struct UnknownErrorCtx {
    safe::ExceptionLocation location;
    std::exception_ptr eptr;
};

@interface SFXUnknownErrorDelegate : NSObject

@property NSWindow *window;

- (void)unknownErrorResponse:(NSAlert *)alert
                  returnCode:(NSInteger)returnCode
                 contextInfo:(void *)contextInfo;

@end

@implementation SFXUnknownErrorDelegate

- (void)unknownErrorResponse:(NSAlert *)alert
returnCode:(NSInteger)returnCode
contextInfo:(void *)contextInfo {
    (void) alert;
    auto ctx = std::unique_ptr<UnknownErrorCtx>((UnknownErrorCtx *) contextInfo);
    if (returnCode == NSAlertSecondButtonReturn) {
        safe::report_exception(ctx->location, ctx->eptr);
    }
    [alert.window orderOut:self];
    [self.window performClose:self];
}

@end

void
runUnknownErrorSheet(NSWindow *window,
                     NSString *title,
                     NSString *message,
                     safe::ExceptionLocation location,
                     const std::exception_ptr & eptr) {
    message = [message stringByAppendingString:safe::mac::to_ns_string(" Please help us improve by sending a bug report. It's automatic and "
                                                                       "no personal information is used.")];

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:title];
    [alert setInformativeText:message];
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Report Bug"];
    [alert setAlertStyle:NSWarningAlertStyle];

    SFXUnknownErrorDelegate *del = [[SFXUnknownErrorDelegate alloc] init];
    del.window = window;

    objc_setAssociatedObject(alert, @selector(unknownErrorDelegate), del, OBJC_ASSOCIATION_RETAIN);

    [alert beginSheetModalForWindow:window
                      modalDelegate:del
                     didEndSelector:@selector(unknownErrorResponse:returnCode:contextInfo:)
                        contextInfo:(void *) new UnknownErrorCtx { location, eptr }];
}
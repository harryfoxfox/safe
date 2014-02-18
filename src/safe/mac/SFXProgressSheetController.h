//
//  SFXProgressSheetController.h
//  Safe
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#include <type_traits>
#include <exception>

@interface SFXProgressSheetController : NSWindowController
@property (weak) IBOutlet NSTextField *msgTextField;
@property (weak) IBOutlet NSProgressIndicator *progressIndicator;

- (void)didEndSheet:(NSWindow *)sheet returnCode:(NSInteger)returnCode contextInfo:(void *)contextInfo;

@end

inline
SFXProgressSheetController *
createAndStartProgressSheet(NSWindow *w, NSString *msg) {
    SFXProgressSheetController *progressSheet =
    [[SFXProgressSheetController alloc] initWithWindowNibName:@"SFXProgressSheetController"];
    
    // load the window... (otherwise our sub properties will be nil)
    (void)progressSheet.window;
    
    [progressSheet.progressIndicator startAnimation:progressSheet];
    [progressSheet.msgTextField setStringValue:msg];
    
    [NSApp beginSheet:progressSheet.window
       modalForWindow:w
        modalDelegate:progressSheet
       didEndSelector:@selector(didEndSheet:returnCode:contextInfo:)
          contextInfo:nil];
    
    return progressSheet;
}

template<class R, class F, class ...Args>
// TODO: maybe enable if typename std::result_of<F(Args...)>::type is convertible to R
void
showBlockingSheetMessage(NSWindow *w,
                         NSString *msg,
                         void (^onSuccess)(R),
                         void (^onException)(const std::exception_ptr &),
                         F f,
                         Args... args) {
    // NB: this method would be a lot more annoying to write without ARC
    SFXProgressSheetController *progressSheet = createAndStartProgressSheet(w, msg);

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       try {
                           auto res = f(args...);
                           auto resp = new decltype(res)(std::move(res));
                           dispatch_async(dispatch_get_main_queue(),
                                          ^{
                                              auto res2 = std::move(*resp);
                                              delete resp;
                                              [NSApp endSheet:progressSheet.window];
                                              onSuccess(std::move(res2));
                                          });
                       }
                       catch (...) {
                           auto eptr = std::current_exception();
                           dispatch_async(dispatch_get_main_queue(),
                                          ^{
                                              [NSApp endSheet:progressSheet.window];
                                              onException(eptr);
                                          });
                       }
                   });
};

template<class F, class ...Args>
void
showBlockingSheetMessageNoRet(NSWindow *w,
                              NSString *msg,
                              void (^onSuccess)(void),
                              void (^onException)(const std::exception_ptr &),
                              F f,
                              Args... args) {
    // NB: this method would be a lot more annoying to write without ARC
    SFXProgressSheetController *progressSheet = createAndStartProgressSheet(w, msg);
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       try {
                           f(args...);
                           dispatch_async(dispatch_get_main_queue(),
                                          ^{
                                              [NSApp endSheet:progressSheet.window];
                                              onSuccess();
                                          });
                       }
                       catch (...) {
                           auto eptr = std::current_exception();
                           dispatch_async(dispatch_get_main_queue(),
                                          ^{
                                              [NSApp endSheet:progressSheet.window];
                                              onException(eptr);
                                          });
                       }
                   });
};

inline
void
inputErrorAlert(NSWindow *w, NSString *title, NSString *msg) {
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:title];
    [alert setInformativeText:msg];
    [alert addButtonWithTitle:@"OK"];
    [alert setAlertStyle:NSWarningAlertStyle];
    
    [alert beginSheetModalForWindow:w
                      modalDelegate:nil
                     didEndSelector:nil
                        contextInfo:nil];
}

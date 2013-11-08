//
//  LBXProgressSheetController.h
//  Lockbox
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#include <type_traits>
#include <exception>

@interface LBXProgressSheetController : NSWindowController
@property (weak) IBOutlet NSTextField *msgTextField;
@property (weak) IBOutlet NSProgressIndicator *progressIndicator;

- (void)didEndSheet:(NSWindow *)sheet returnCode:(NSInteger)returnCode contextInfo:(void *)contextInfo;

@end

inline
LBXProgressSheetController *
createAndStartProgressSheet(NSWindow *w, NSString *msg) {
    LBXProgressSheetController *progressSheet =
    [[LBXProgressSheetController alloc] initWithWindowNibName:@"LBXProgressSheetController"];
    
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

template<class F, class ...Args>
void
showBlockingSheetMessage(NSWindow *w,
                         NSString *msg,
                         void (^onSuccess)(const typename std::result_of<F(Args...)>::type &),
                         void (^onException)(const std::exception_ptr &),
                         F f,
                         Args... args) {
    // NB: this method would be a lot more annoying to write without ARC
    LBXProgressSheetController *progressSheet = createAndStartProgressSheet(w, msg);

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       try {
                           auto res = f(args...);
                           dispatch_async(dispatch_get_main_queue(),
                                          ^{
                                              [NSApp endSheet:progressSheet.window];
                                              onSuccess(res);
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
    LBXProgressSheetController *progressSheet = createAndStartProgressSheet(w, msg);
    
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
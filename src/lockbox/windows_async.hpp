/*
  Lockbox: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef __lockbox_windows_async_hpp
#define __lockbox_windows_async_hpp

#include <lockbox/windows_dialog.hpp>
#include <lockbox/windows_gui_util.hpp>
#include <lockbox/util.hpp>

#include <davfuse/logging.h>

#include <stdexcept>
#include <string>

#include <cstddef>

#include <lockbox/lean_windows.h>

#include <commctrl.h>

#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER + 10)
#endif

#ifndef PBS_MARQUEE
#define PBS_MARQUEE 8
#endif


namespace w32util {

const int IDC_PROGRESS = 2;

CALLBACK
inline
BOOL
_modal_call_dialog_proc(HWND hwnd, UINT Message,
                        WPARAM /*wParam*/, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG: {
    w32util::center_window_in_monitor(hwnd);
    // disable close button
    EnableMenuItem(GetSystemMenu(hwnd, FALSE), SC_CLOSE,
                   MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);

    w32util::set_default_dialog_font(hwnd);

    auto ret = SendDlgItemMessageW(hwnd, IDC_PROGRESS,
                                   PBM_SETMARQUEE, TRUE, 0);
    if (!ret) log_error("Error while setting marquee");
    return TRUE;
  }
  case WM_DESTROY:
    w32util::cleanup_default_dialog_font(hwnd);
    // we don't actually handle this
    return FALSE;
  default:
    return FALSE;
  }
  return TRUE;
}

WINAPI
inline
HWND
create_waiting_modal(HWND parent, std::string title, std::string ui_msg) {
  typedef WORD unit_t;
  const unit_t DIALOG_WIDTH = 150;

  const unit_t TOP_MARGIN = 12;
  const unit_t MIDDLE_MARGIN = 12;
  const unit_t BOTTOM_MARGIN = 12;

  const unit_t TEXT_HEIGHT = 24;
  const unit_t TEXT_WIDTH = DIALOG_WIDTH - 10;

  const unit_t PROGRESS_HEIGHT = 12;
  const unit_t PROGRESS_WIDTH = DIALOG_WIDTH - 10;

  const WORD IDC_IGNORE = ~0;
  auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              std::move(title),
                              0, 0, DIALOG_WIDTH,
                              TOP_MARGIN + TEXT_HEIGHT +
                              MIDDLE_MARGIN + PROGRESS_HEIGHT +
                              BOTTOM_MARGIN),
                   {
                     DialogItemDesc(SS_CENTER | SS_CENTERIMAGE |
                                    WS_VISIBLE | WS_CHILD,
                                    std::move(ui_msg),
                                    ControlClass::STATIC,
                                    IDC_IGNORE,
                                    center_offset(DIALOG_WIDTH, TEXT_WIDTH),
                                    TOP_MARGIN,
                                    TEXT_WIDTH, TEXT_HEIGHT),
                     Control("", IDC_PROGRESS, PROGRESS_CLASSW, PBS_MARQUEE,
                             center_offset(DIALOG_WIDTH, PROGRESS_WIDTH),
                             TOP_MARGIN + TEXT_HEIGHT + MIDDLE_MARGIN,
                             PROGRESS_WIDTH, PROGRESS_HEIGHT),
                   }
                   );

  return CreateDialogIndirect(NULL,
                              dlg.get_data(),
                              parent, _modal_call_dialog_proc);
}

WINAPI
inline
std::unique_ptr<MSG>
modal_until_message(HWND parent, std::string title, std::string ui_msg,
                    UINT msg) {
  auto dialog_wnd = create_waiting_modal(parent,
                                         std::move(title),
                                         std::move(ui_msg));
  if (!dialog_wnd) return nullptr;
  auto _destroy_dialog_wnd = lockbox::create_destroyer(dialog_wnd, DestroyWindow);

  // disable window
  if (parent) EnableWindow(parent, FALSE);

  // loop
  auto getmsg = std::unique_ptr<MSG>(new MSG);
  BOOL ret_getmsg;
  while ((ret_getmsg = GetMessageW(getmsg.get(), NULL, 0, 0))) {
    if (ret_getmsg == -1) break;
    if (getmsg->message == msg) break;
    if (!IsDialogMessage(dialog_wnd, getmsg.get())) {
      TranslateMessage(getmsg.get());
      DispatchMessage(getmsg.get());
    }
  }

  // re-enable window
  if (parent) EnableWindow(parent, TRUE);

  if (ret_getmsg == -1) return nullptr;

  if (getmsg->message == WM_QUIT) {
    PostQuitMessage(getmsg->wParam);
    return nullptr;
  }

  return std::move(getmsg);
}

WINAPI
inline
void
modal_until_object(HWND parent, std::string title, std::string ui_msg,
                   HANDLE obj) {
  auto dialog_wnd = create_waiting_modal(parent,
                                         std::move(title),
                                         std::move(ui_msg));
  if (!dialog_wnd) throw w32util::windows_error();

  auto _destroy_dialog_wnd = lockbox::create_destroyer(dialog_wnd, DestroyWindow);

  // disable window
  decltype(lockbox::create_deferred(EnableWindow, parent, TRUE)) _renable;
  if (parent) {
    EnableWindow(parent, FALSE);
    _renable = lockbox::create_deferred(EnableWindow, parent, TRUE);
  }

  // loop
  while (true) {
    auto ret = MsgWaitForMultipleObjects(1, &obj, TRUE, INFINITE, QS_ALLEVENTS);
    if (ret == WAIT_OBJECT_0) break;
    if (ret != WAIT_OBJECT_0 + 1) throw w32util::windows_error();

    MSG getmsg;
    while (PeekMessageW(&getmsg, NULL, 0, 0, PM_REMOVE)) {
      if (!IsDialogMessage(dialog_wnd, &getmsg)) {
        TranslateMessage(&getmsg);
        DispatchMessage(&getmsg);
      }
    }
  }
}

WINAPI
inline
DWORD
_modal_call_thread_proc(LPVOID param) {
  auto fn = (std::function<void()> *) param;
  (*fn)();
  return 0;
}

template<class FunctionB, class FunctionA, class Function, class... Args>
opt::optional<typename std::result_of<Function(Args...)>::type>
run_async_base(FunctionB && loop_stop,
               FunctionA && loop_wait,
               Function && f, Args &&... args) {
  // place to store result
  std::exception_ptr eptr;
  opt::optional<typename std::result_of<Function(Args...)>::type> ret_store;
  auto pre_bound_fn = std::bind(f, std::forward<Args>(args)...);

  std::function<void()> bound_fn = [&] () {
    try {
      ret_store = pre_bound_fn();
    }
    catch (...) {
      eptr = std::current_exception();
    }
    // wake up main thread here
    loop_stop();
  };

  // start thread with fn
  auto ret_thread =
    CreateThread(NULL, 0, _modal_call_thread_proc,
                 (LPVOID) &bound_fn, 0, NULL);
  if (!ret_thread) {
    throw std::runtime_error("Couldn't create thread");
  }
  auto _close_thread = lockbox::create_destroyer(ret_thread, CloseHandle);

  auto received_stop = (bool) loop_wait();

  if (!received_stop) {
    // the loop failed
    // we need to ensure the thread is no longer running
    // (since it has access to our stack variables)
    WaitForSingleObject(ret_thread, INFINITE);
    // (Yes terminate thread is a bit hard core
    // DWORD exit_code;
    // TerminateThread(ret_thread, &exit_code);
    return opt::nullopt;
  }

  assert((bool) ret_store == !(bool) eptr);

  if (!ret_store) std::rethrow_exception(eptr);
  else return std::move(ret_store);
}

// calls the input function on a different thread
// while still running the message loop
template<class Function, class... Args>
opt::optional<typename std::result_of<Function(Args...)>::type>
modal_call(HWND parent, std::string title, std::string msg,
           Function && f, Args &&... args) {
  const UINT break_msg = WM_APP;

  DWORD cur_tid = GetCurrentThreadId();
  auto loop_stop = [&] () {
    PostThreadMessage(cur_tid, break_msg, 0, 0);
  };

  auto loop_wait = [&] () {
    return modal_until_message(parent, title, msg, break_msg);
  };

  return run_async_base(loop_stop, loop_wait,
                        std::forward<Function>(f),
                        std::forward<Args>(args)...);
}

template<class Function, class... Args>
bool
modal_call_void(HWND parent, std::string title, std::string msg,
                Function && f, Args &&... args) {
  auto new_func = [&] (Args &&... args2) {
    f(std::forward<Args>(args2)...);
    return true;
  };

  auto ret = modal_call(parent, std::move(title), std::move(msg),
                        std::move(new_func), std::forward<Args>(args)...);
  return (bool) ret;
}

template<class Function, class... Args>
bool
run_async_void(Function && f, Args &&... args) {
  const UINT break_msg = WM_APP;

  DWORD cur_tid = GetCurrentThreadId();
  auto loop_stop = [&] () {
    PostThreadMessage(cur_tid, break_msg, 0, 0);
  };

  auto loop_wait = [&] () {
    // loop
    MSG msg;
    BOOL ret_getmsg;
    while ((ret_getmsg = GetMessageW(&msg, NULL, 0, 0))) {
      if (ret_getmsg == -1) break;
      if (msg.message == break_msg) break;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    if (ret_getmsg == -1) return false;

    if (msg.message == WM_QUIT) {
      PostQuitMessage(msg.wParam);
      return false;
    }

    return true;
  };

  std::function<bool(Args...)> new_func = [&] (Args &&... args2) {
    f(std::forward<Args>(args2)...);
    return true;
  };

  auto ret =
    run_async_base(loop_stop, loop_wait,
                   std::move(new_func),
                   std::forward<Args>(args)...);

  return (bool) ret;
}

}

#endif

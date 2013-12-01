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

#include <lockbox/product_name.h>
#include <lockbox/lockbox_server.hpp>
#include <lockbox/lockbox_strings.h>
#include <lockbox/logging.h>
#include <lockbox/mount_win.hpp>
#include <lockbox/windows_about_dialog.hpp>
#include <lockbox/windows_app_actions.hpp>
#include <lockbox/windows_async.hpp>
#include <lockbox/windows_create_lockbox_dialog.hpp>
#include <lockbox/windows_string.hpp>
#include <lockbox/windows_gui_util.hpp>
#include <lockbox/util.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/fs/FileUtils.h>

#include <encfs/cipher/MemoryPool.h>

#include <encfs/base/optional.h>

#include <davfuse/event_loop.h>
#include <davfuse/log_printer.h>
#include <davfuse/webdav_server.h>

#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <sstream>

#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <Shellapi.h>
#include <dbt.h>

// TODO:
// 2) Icons
// 3) Unmount when webdav server unexpectedly stops (defensive coding)

struct DestroyMenuDeleter {
  void operator()(HMENU a) {
    auto ret = DestroyMenu(a);
    if (!ret) throw std::runtime_error("couldn't free!");
  }
};

typedef lockbox::ManagedResource<HMENU, DestroyMenuDeleter> ManagedMenuHandle;

struct WindowData {
  std::shared_ptr<encfs::FsIO> native_fs;
  std::vector<lockbox::win::MountDetails> mounts;
  bool is_stopping;
  UINT TASKBAR_CREATED_MSG;
  UINT LOCKBOX_DUPLICATE_INSTANCE_MSG;
  HWND last_foreground_window;
  bool popup_menu_is_open;
};

// constants
const unsigned MOUNT_RETRY_ATTEMPTS = 10;
const WORD IDC_STATIC = ~0;
const WORD IDPASSWORD = 200;
const WORD IDCONFIRMPASSWORD = 201;
const auto MAX_PASS_LEN = 256;
const wchar_t TRAY_ICON_TOOLTIP[] = PRODUCT_NAME_W;
const wchar_t MAIN_WINDOW_CLASS_NAME[] = L"lockbox_tray_icon";
const UINT_PTR STOP_RELEVANT_DRIVE_THREADS_TIMER_ID = 0;

const auto APP_BASE = (UINT) (6 + WM_APP);
const auto LOCKBOX_MOUNT_DONE_MSG = APP_BASE;
const auto LOCKBOX_MOUNT_OVER_MSG = APP_BASE + 1;
const auto LOCKBOX_TRAY_ICON_MSG = APP_BASE + 2;
const auto LOCKBOX_TRAY_ICON_MSG_2 = APP_BASE + 3;

const auto LOCKBOX_TRAY_ICON_ID = 1;

const auto MENU_MOUNT_IDX_BASE = (UINT) 1;
const auto MENU_BASE = MENU_MOUNT_IDX_BASE + 26;
const auto MENU_CREATE_ID = MENU_BASE + 0;
const auto MENU_QUIT_ID = MENU_BASE + 1;
const auto MENU_DEBUG_ID = MENU_BASE + 2;
const auto MENU_GETSRCCODE_ID = MENU_BASE + 3;
const auto MENU_TEST_BUBBLE_ID = MENU_BASE + 4;

const wchar_t LOCKBOX_SINGLE_APP_INSTANCE_MUTEX_NAME[] =
  L"LockboxAppMutex";
const wchar_t LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME[] =
  L"LockboxAppSharedProcessId";
const wchar_t LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME[] =
  L"LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME";
const wchar_t TASKBAR_CREATED_MESSAGE_NAME[] =
  L"TaskbarCreated";
const char LOCKBOX_TRAY_ICON_WELCOME_TITLE[] =
  PRODUCT_NAME_A " is now Running!";
const char LOCKBOX_TRAY_ICON_WELCOME_MSG[] =
  "If you need to use "
  PRODUCT_NAME_A
  ", just right-click on this icon.";


UINT
mount_idx_to_menu_id(unsigned idx) {
  return idx + MENU_MOUNT_IDX_BASE;
}

unsigned
menu_id_to_mount_idx(UINT id) {
  assert(id >= MENU_MOUNT_IDX_BASE);
  return id - MENU_MOUNT_IDX_BASE;
}

static
void
bubble_msg(HWND lockbox_main_window,
           const std::string title, const std::string &msg);

static
void
stop_drive_thread(lockbox::win::MountDetails *md) {
  md->signal_stop();
}

static
std::vector<lockbox::win::MountDetails>::iterator
stop_drive(HWND lockbox_main_window,
           WindowData & wd,
           std::vector<lockbox::win::MountDetails>::iterator it) {
  // remove mount from list (even if thread hasn't died)
  auto md = std::move(*it);
  it = wd.mounts.erase(it);

  // then stop thread
  auto success =
    w32util::run_async_void(stop_drive_thread, &md);
  // if not success, it was a quit message
  if (!success) return it;

  // TODO: this might be too spammy if multiple drives have been
  // unmounted
  bubble_msg(lockbox_main_window,
             "Success",
             "You've successfully stopped \"" + md.get_mount_name() +
             ".\"");

  return it;
}

static
void
stop_relevant_drive_threads(HWND lockbox_main_window, WindowData & wd) {
  assert(!wd.popup_menu_is_open);

  for (auto it = wd.mounts.begin(); it != wd.mounts.end();) {
    if (!it->is_still_mounted()) {
      it = stop_drive(lockbox_main_window, wd, it);
    }
    else ++it;
  }
}

static
void
append_string_menu_item(HMENU menu_handle, bool is_default,
                        std::string text, UINT id) {
  auto menu_item_text =
    w32util::widen(std::move(text));

  MENUITEMINFOW mif;
  lockbox::zero_object(mif);

  mif.cbSize = sizeof(mif);
  mif.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_STRING;
  mif.fType = MFT_STRING;
  mif.fState = is_default ? MFS_DEFAULT : 0;
  mif.wID = id;
  mif.dwTypeData = const_cast<LPWSTR>(menu_item_text.data());
  mif.cch = menu_item_text.size();

  auto items_added = GetMenuItemCount(menu_handle);
  if (items_added == -1) throw std::runtime_error("GetMenuItemCount");

  auto success_menu_item =
    InsertMenuItemW(menu_handle, items_added, TRUE, &mif);
  if (!success_menu_item) {
    throw std::runtime_error("InsertMenuItem elt");
  }
}

static
bool
alert_of_popup_if_we_have_one(HWND lockbox_main_window, const WindowData & wd,
                              bool beep = true) {
  auto child = GetWindow(lockbox_main_window, GW_ENABLEDPOPUP);
  if (child) {
    SetForegroundWindow(child);

    if (beep && child == wd.last_foreground_window) {
      DWORD flash_count;
      auto success =
        SystemParametersInfoW(SPI_GETFOREGROUNDFLASHCOUNT,
                              0, &flash_count, 0);
      if (!success) throw w32util::windows_error();

      FLASHWINFO finfo;
      lockbox::zero_object(finfo);
      finfo.cbSize = sizeof(finfo);
      finfo.hwnd = child;
      finfo.dwFlags = FLASHW_ALL;
      finfo.uCount = flash_count;
      finfo.dwTimeout = 66;

      FlashWindowEx(&finfo);
      MessageBeep(MB_OK);
    }
  }

  return (bool) child;
}

WINAPI
bool
open_create_mount_dialog(HWND lockbox_main_window, WindowData & wd) {
  // only run this dialog if we don't already have a dialog
  (void) lockbox_main_window;
  (void) wd;
  assert(!GetWindow(lockbox_main_window, GW_ENABLEDPOPUP));
  // TODO: implement
  return false;
}

void
unmount_and_stop_drive(HWND hwnd, WindowData & wd, size_t mount_idx) {
  assert(!wd.popup_menu_is_open);

  auto mount_p = wd.mounts.begin() + mount_idx;

  // first unmount drive
  mount_p->unmount();

  // now remove mount from list
  stop_drive(hwnd, wd, mount_p);
}

static
void
perform_default_lockbox_action(HWND lockbox_main_window, WindowData & wd) {
  auto child = alert_of_popup_if_we_have_one(lockbox_main_window, wd, false);
  if (child) return;

  // if there is an active mount, open the folder
  // otherwise allow the user to make a new mount
  if (wd.mounts.empty()) open_create_mount_dialog(lockbox_main_window, wd);
  else {
    try {
      wd.mounts.front().open_mount();
    }
    catch (const std::exception & err) {
      lbx_log_error("Error while opening mount \"%s\": %s",
                    wd.mounts.front().get_mount_name().c_str(),
                    err.what());
    }
  }
}

static
void
bubble_msg(HWND lockbox_main_window,
           const std::string title, const std::string &msg) {
  NOTIFYICONDATAW icon_data;
  lockbox::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = lockbox_main_window;
  icon_data.uID = LOCKBOX_TRAY_ICON_ID;
  icon_data.uFlags = NIF_INFO;
  icon_data.uVersion = NOTIFYICON_VERSION;

  auto wmsg = w32util::widen(msg);
  if (wmsg.size() >= sizeof(icon_data.szInfo)) {
    throw std::runtime_error("msg is too long!");
  }
  wmemcpy(icon_data.szInfo, wmsg.c_str(), wmsg.size() + 1);
  icon_data.uTimeout = 0; // let it be the minimum

  auto wtitle = w32util::widen(title);
  if (wtitle.size() >= sizeof(icon_data.szInfoTitle)) {
    throw std::runtime_error("title is too long!");
  }
  wmemcpy(icon_data.szInfoTitle, wtitle.c_str(), wtitle.size() + 1);

  icon_data.dwInfoFlags = NIIF_NONE;

  auto success = Shell_NotifyIconW(NIM_MODIFY, &icon_data);
  if (!success) throw w32util::windows_error();
}

ManagedMenuHandle
create_lockbox_menu(const WindowData & wd, bool is_control_pressed) {
  // okay our menu is like this
  // [ Lockbox A ]
  // [ Lockbox B ]
  // ...
  // [ ------- ]
  // Create New...
  // Mount Existing...
  // Mount Recent >
  //   Lockbox A
  //   Lockbox B
  //   ---------
  //   Clear
  // -----------
  // About
  // [ DebugBreak ]
  // [ Test Bubble ]
  // -----------
  // Quit

  auto menu_handle = CreatePopupMenu();
  if (!menu_handle) throw std::runtime_error("CreatePopupMenu");
  auto to_ret = ManagedMenuHandle(menu_handle);

  const auto action = is_control_pressed
    ? std::string("Stop")
    : std::string("Open");

  UINT idx = 0;
  for (const auto & md : wd.mounts) {
    append_string_menu_item(menu_handle,
                            idx == 0,
                            (action + " \"" + md.get_mount_name() + "\" (" +
                             std::to_string(md.get_drive_letter()) + ":)"),
                            mount_idx_to_menu_id(idx));
    ++idx;
  }

  // add separator
  if (!wd.mounts.empty()) {
    MENUITEMINFOW mif;
    lockbox::zero_object(mif);

    mif.cbSize = sizeof(mif);
    mif.fMask = MIIM_FTYPE;
    mif.fType = MFT_SEPARATOR;

    auto items_added = GetMenuItemCount(menu_handle);
    if (items_added == -1) throw std::runtime_error("GetMenuItemCount");
    auto success_menu_item =
      InsertMenuItemW(menu_handle, items_added, TRUE, &mif);
    if (!success_menu_item) {
      throw std::runtime_error("InsertMenuItem sep");
    }
  }

  // add create action
  append_string_menu_item(menu_handle,
                          wd.mounts.empty(),
                          "Start or create a " ENCRYPTED_STORAGE_NAME_A,
                          MENU_CREATE_ID);

  // add get source code action
  append_string_menu_item(menu_handle,
                          false,
                          "Get source code",
                          MENU_GETSRCCODE_ID);

  // add quit action
  append_string_menu_item(menu_handle,
                          false,
                          "Exit",
                          MENU_QUIT_ID);

#ifndef NDEBUG
  // add breakpoint action
  append_string_menu_item(menu_handle,
                          false,
                          "DebugBreak",
                          MENU_DEBUG_ID);

  // add breakpoint action
  append_string_menu_item(menu_handle,
                          false,
                          "Test Bubble",
                          MENU_TEST_BUBBLE_ID);
#endif

  return std::move(to_ret);
}

static
void
add_tray_icon(HWND lockbox_main_window) {
  NOTIFYICONDATAW icon_data;
  lockbox::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = lockbox_main_window;
  icon_data.uID = LOCKBOX_TRAY_ICON_ID;
  icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon_data.uCallbackMessage = LOCKBOX_TRAY_ICON_MSG;
  icon_data.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  icon_data.uVersion = NOTIFYICON_VERSION;
  memcpy(icon_data.szTip, TRAY_ICON_TOOLTIP, sizeof(TRAY_ICON_TOOLTIP));

  auto success = Shell_NotifyIconW(NIM_ADD, &icon_data);
  if (!success) {
    // TODO deal with this?
    lbx_log_error("Error while adding icon: %s",
                  w32util::last_error_message().c_str());
  }
  else {
    // now set tray icon version
    auto success_version = Shell_NotifyIcon(NIM_SETVERSION, &icon_data);
    if (!success_version) {
      // TODO: deal with this
      lbx_log_error("Error while setting icon version: %s",
                    w32util::last_error_message().c_str());
    }
  }
}

#define NO_FALLTHROUGH(c) assert(false); case c

CALLBACK
LRESULT
main_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const auto wd = (WindowData *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
  const auto TASKBAR_CREATED_MSG = wd ? wd->TASKBAR_CREATED_MSG : 0;
  const auto LOCKBOX_DUPLICATE_INSTANCE_MSG =
    wd ? wd->LOCKBOX_DUPLICATE_INSTANCE_MSG : 0;
  switch(msg){
    NO_FALLTHROUGH(LOCKBOX_MOUNT_OVER_MSG): {
      // TODO: handle this
      return 0;
    }

    NO_FALLTHROUGH(WM_TIMER): {
      if (wParam == STOP_RELEVANT_DRIVE_THREADS_TIMER_ID) {
        if (wd->popup_menu_is_open) {
          // delay message until menu is closed
          auto success =
            SetTimer(hwnd, STOP_RELEVANT_DRIVE_THREADS_TIMER_ID, 0, NULL);
          if (!success) throw w32util::windows_error();
        } else if (!wd->is_stopping) {
          wd->is_stopping = true;
          // if this throws an exception, our app is going to close
          stop_relevant_drive_threads(hwnd, *wd);
          wd->is_stopping = false;
        }
        return 0;
      }
      else {
        // we didn't process this timer
        return -1;
      }
    }

    NO_FALLTHROUGH(WM_DEVICECHANGE): {
      // NB: stop the thread on a different message
      //     since the OS blocks on the return from this message dispatch
      //     to actually unmount the drive
      if (wParam == DBT_DEVICEREMOVECOMPLETE) {
        auto success =
          SetTimer(hwnd, STOP_RELEVANT_DRIVE_THREADS_TIMER_ID, 0, NULL);
        // TODO: we don't yet handle this gracefully
        if (!success) throw w32util::windows_error();
      }
      return TRUE;
    }

    NO_FALLTHROUGH(WM_CREATE): {
      // set application state (WindowData) in main window
      const auto wd = (WindowData *) ((LPCREATESTRUCT) lParam)->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) wd);

      // add tray icon
      add_tray_icon(hwnd);

      // open about dialog
      // TODO: only do this if we've never done this before
      lockbox::win::about_dialog(hwnd);

      bubble_msg(hwnd,
                 LOCKBOX_TRAY_ICON_WELCOME_TITLE,
                 LOCKBOX_TRAY_ICON_WELCOME_MSG);

      // return -1 on failure, CreateWindow* will return NULL
      return 0;
    }

    NO_FALLTHROUGH(LOCKBOX_TRAY_ICON_MSG): {
      switch (lParam) {
        NO_FALLTHROUGH(1029): {
          lbx_log_debug("User clicked balloon");
          break;
        }

        NO_FALLTHROUGH(WM_MOUSEMOVE): {
          wd->last_foreground_window = GetForegroundWindow();
          break;
        }

        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
          alert_of_popup_if_we_have_one(hwnd, *wd);
          break;
        }

        NO_FALLTHROUGH(WM_LBUTTONDBLCLK): {
          perform_default_lockbox_action(hwnd, *wd);
          break;
        }

        // NB: Must popup menu on WM_RBUTTONUP presumably because the menu
        //     closes when it gets when clicked off WM_RBUTTONDOWN 
        //     (otherwise menu will activate and then immediately close)
        NO_FALLTHROUGH(WM_RBUTTONUP): {
          // repost a top-level message to the window
          // since sometimes these messages come while the menu is still
          // being "tracked" (this is a workaround for a bug in windows)
          POINT xypos;
          auto success_pos = GetCursorPos(&xypos);
          if (!success_pos) throw std::runtime_error("GetCursorPos");

          const auto is_control_pressed =
            GetKeyState(VK_CONTROL) >>
            (sizeof(GetKeyState(VK_CONTROL)) * 8 - 1);

          PostMessage(hwnd, LOCKBOX_TRAY_ICON_MSG_2,
                      (LPARAM) is_control_pressed,
                      MAKELPARAM(xypos.x, xypos.y));
          break;
        }

      default: break;
      }

      // Always return 0 for all tray icon messages
      return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    NO_FALLTHROUGH(LOCKBOX_TRAY_ICON_MSG_2): {
      {
        //only do the action if we don't have a dialog up
        auto child = alert_of_popup_if_we_have_one(hwnd, *wd, false);
        if (child) return 0;
      }

      if (wd->popup_menu_is_open) return 0;

      try {
        auto is_control_pressed = (bool) wParam;
        auto menu_handle = create_lockbox_menu(*wd, is_control_pressed);

        // NB: have to do this so the menu disappears
        // when you click out of it
        SetForegroundWindow(hwnd);

        wd->popup_menu_is_open = true;
        SetLastError(0);
        auto selected =
          (UINT) TrackPopupMenu(menu_handle.get(),
                                TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                                TPM_RIGHTBUTTON |
                                TPM_NONOTIFY | TPM_RETURNCMD,
                                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                0, hwnd, NULL);
        wd->popup_menu_is_open = false;

        if (!selected) throw std::runtime_error("TrackPopupMenu");

        // according to MSDN, we must force a "task switch"
        // this is so that if the user attempts to open the
        // menu while the menu is open, it reopens instead
        // of disappearing
        PostMessage(hwnd, WM_NULL, 0, 0);

        switch (selected) {
        case 0:
          // menu was canceled
          break;
        case MENU_QUIT_ID: {
          DestroyWindow(hwnd);
          break;
        }
        case MENU_CREATE_ID: {
          open_create_mount_dialog(hwnd, *wd);
          break;
        }
        case MENU_DEBUG_ID: {
          DebugBreak();
          break;
        }
        case MENU_GETSRCCODE_ID: {
          lockbox::win::open_src_code(hwnd);
          break;
        }
        case MENU_TEST_BUBBLE_ID: {
          bubble_msg(hwnd, "Short Title",
                     "Very long message full of meaningful info that you "
                     "will find very interesting because you love to read "
                     "tray icon bubbles. Don't you? Don't you?!?!");
          break;
        }
        default: {
          const auto selected_mount_idx = menu_id_to_mount_idx(selected);
          assert(selected_mount_idx < wd->mounts.size());
          if (is_control_pressed) {
            unmount_and_stop_drive(hwnd, *wd, selected_mount_idx);
          }
          else {
            wd->mounts[selected_mount_idx].open_mount();
          }
          break;
        }
        }
      }
      catch (const std::exception & err) {
        lbx_log_error("Error while doing: \"%s\": %s (%lu)",
                      err.what(),
                      w32util::last_error_message().c_str(),
                      GetLastError());
      }

      return 0;
    }

    NO_FALLTHROUGH(WM_CLOSE): {
      DestroyWindow(hwnd);
      return 0;
    }

    NO_FALLTHROUGH(WM_DESTROY): {
      // remove tray icon
      NOTIFYICONDATA icon_data;
      lockbox::zero_object(icon_data);
      icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
      icon_data.hWnd = hwnd;
      icon_data.uID = LOCKBOX_TRAY_ICON_ID;
      icon_data.uVersion = NOTIFYICON_VERSION;

      Shell_NotifyIcon(NIM_DELETE, &icon_data);

      // quit application
      PostQuitMessage(0);
      return 0;
    }

  default:
    if (msg == TASKBAR_CREATED_MSG) {
      // re-add tray icon
      add_tray_icon(hwnd);
    }
    else if (msg == LOCKBOX_DUPLICATE_INSTANCE_MSG){
      // do the current default action
      perform_default_lockbox_action(hwnd, *wd);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  /* not reached, there is no default return code */
  assert(false);
}

static
void
exit_if_not_single_app_instance(std::function<int(DWORD)> on_exit) {
  // NB: we abort() instead of throwing exceptions
  //     we exit() instead of returning a boolean
  //     this function must ensure a single app instance and
  //     this is the best contract for that, otherwise
  //     a user could misuse this function and potentially continue
  //     executing

  SetLastError(0);
  auto mutex_handle =
    CreateMutexW(NULL, TRUE, LOCKBOX_SINGLE_APP_INSTANCE_MUTEX_NAME);
  switch (GetLastError()) {
  case ERROR_SUCCESS: {
    // write our process id in a shared memory
    auto mapping_handle =
      CreateFileMapping(INVALID_HANDLE_VALUE,
                        NULL, PAGE_READWRITE,
                        0, sizeof(DWORD),
                        LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME);
    if (!mapping_handle) abort();

    const auto proc_id_p =
      (DWORD *) MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS,
                              0, 0, sizeof(DWORD));
    if (!proc_id_p) abort();

    *proc_id_p = GetCurrentProcessId();

    auto success_unmap = UnmapViewOfFile(proc_id_p);
    if (!success_unmap) abort();

    auto success_release = ReleaseMutex(mutex_handle);
    if (!success_release) abort();

    // NB: we leave the mutex_handle and mapping_handle open
    return;
  }
  case ERROR_ALREADY_EXISTS: {
    // acquire mutex to shared memory
    auto wait_ret = WaitForSingleObject(mutex_handle, INFINITE);
    if (wait_ret != WAIT_OBJECT_0) abort();

    // read process id from shared memory
    auto mapping_handle =
      OpenFileMapping(FILE_MAP_ALL_ACCESS,
                      FALSE,
                      LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME);
    if (!mapping_handle) abort();

    const auto proc_id_p =
      (DWORD *) MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS,
                              0, 0, sizeof(DWORD));
    if (!proc_id_p) abort();

    const auto proc_id = *proc_id_p;

    auto success_unmap = UnmapViewOfFile(proc_id_p);
    if (!success_unmap) abort();

    auto success_close_mapping = CloseHandle(mapping_handle);
    if (!success_close_mapping) abort();

    auto success_release = ReleaseMutex(mutex_handle);
    if (!success_release) abort();

    auto success_close_mutex = CloseHandle(mutex_handle);
    if (!success_close_mutex) abort();

    if (proc_id != GetCurrentProcessId()) std::exit(on_exit(proc_id));

    return;
  }
  default: {
    lbx_log_error("Error on CreateMutex: %s",
                  w32util::last_error_message().c_str());
    abort();
  }
  }
}

WINAPI
int
WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
        LPSTR /*lpCmdLine*/, int nCmdShow) {
  // TODO: catch all exceptions, since this is a top-level

  // TODO: de-initialize
  log_printer_default_init();
  logging_set_global_level(LOG_DEBUG);
  lbx_log_debug("Hello world!");

  // TODO: de-initialize
  auto ret_ole = OleInitialize(NULL);
  if (ret_ole != S_OK) throw std::runtime_error("couldn't initialize ole!");

  INITCOMMONCONTROLSEX icex;
  lockbox::zero_object(icex);
  icex.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
  icex.dwSize = sizeof(icex);

  auto success = InitCommonControlsEx(&icex);
  if (success == FALSE) {
    throw std::runtime_error("Couldn't initialize common controls");
  }

  // TODO: require ComCtl32.dll version >= 6.0
  // (we do this in the manifest but it would be nice
  //  to check at runtime too)

  // TODO: de-initialize
  lockbox::global_webdav_init();

  auto LOCKBOX_DUPLICATE_INSTANCE_MSG =
    RegisterWindowMessage(LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME);
  if (!LOCKBOX_DUPLICATE_INSTANCE_MSG) {
    throw std::runtime_error("Couldn't get duplicate instance message");
  }

  exit_if_not_single_app_instance([&] (DWORD proc_id) {
      AllowSetForegroundWindow(proc_id);
      PostMessage(HWND_BROADCAST, LOCKBOX_DUPLICATE_INSTANCE_MSG, 0, 0);
      return 0;
    });

  auto TASKBAR_CREATED_MSG =
    RegisterWindowMessage(TASKBAR_CREATED_MESSAGE_NAME);
  if (!TASKBAR_CREATED_MSG) {
    throw std::runtime_error("Couldn't register for TaskbarCreated message");
  }

  auto native_fs = lockbox::create_native_fs();

  lockbox::win::create_new_lockbox_dialog(NULL, native_fs);
  return 0;

  auto wd = WindowData {
    /*.native_fs = */std::move(native_fs),
    /*.mounts = */std::vector<lockbox::win::MountDetails>(),
    /*.is_stopping = */false,
    /*.TASKBAR_CREATED_MSG = */TASKBAR_CREATED_MSG,
    /*.LOCKBOX_DUPLICATE_INSTANCE_MSG = */LOCKBOX_DUPLICATE_INSTANCE_MSG,
    /*.last_foreground_window = */NULL,
    /*.popup_menu_is_open = */false,
  };

  // register our main window class
  WNDCLASSEXW wc;
  lockbox::zero_object(wc);
  wc.cbSize        = sizeof(WNDCLASSEX);
  wc.lpfnWndProc   = main_wnd_proc;
  wc.hInstance     = hInstance;
  wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
  wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) COLOR_WINDOW;
  wc.lpszClassName = MAIN_WINDOW_CLASS_NAME;
  wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);

  if (!RegisterClassExW(&wc)) {
    throw std::runtime_error("Failure registering main window class");
  }

  // create our main window
  auto hwnd = CreateWindowW(MAIN_WINDOW_CLASS_NAME,
                            L"Lockbox Main Window",
                            WS_OVERLAPPED,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                            /*HWND_MESSAGE*/NULL, NULL, hInstance, &wd);
  if (!hwnd) {
    throw std::runtime_error("Failure to create main window");
  }

  // update window
  UpdateWindow(hwnd);
  ShowWindow(hwnd, SW_HIDE);
  (void) nCmdShow;

  // run message loop
  MSG msg;
  BOOL ret_getmsg;
  while ((ret_getmsg = GetMessageW(&msg, NULL, 0, 0))) {
    if (ret_getmsg == -1) break;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // kill all mounts
  for (auto & mount : wd.mounts) {
    try {
      mount.unmount();
    }
    catch (const std::exception & err) {
      lbx_log_error("Failed to unmount \"%s\": %s",
                    mount.get_mount_name().c_str(),
                    err.what());
    }
  }

  if (ret_getmsg == -1) throw std::runtime_error("getmessage failed!");

  return msg.wParam;
}

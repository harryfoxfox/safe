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

#include <lockbox/about_dialog_win.hpp>
#include <lockbox/create_lockbox_dialog_win.hpp>
#include <lockbox/constants.h>
#include <lockbox/fs.hpp>
#include <lockbox/logging.h>
#include <lockbox/mount_lockbox_dialog_win.hpp>
#include <lockbox/mount_win.hpp>
#include <lockbox/recent_paths_storage.hpp>
#include <lockbox/resources_win.h>
#include <lockbox/tray_menu.hpp>
#include <lockbox/tray_menu_win.hpp>
#include <lockbox/webdav_server.hpp>
#include <lockbox/welcome_dialog_win.hpp>
#include <lockbox/windows_async.hpp>
#include <lockbox/windows_gui_util.hpp>
#include <lockbox/windows_menu.hpp>
#include <lockbox/windows_string.hpp>
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
#include <Shlobj.h>

// TODO:
// 3) Unmount when webdav server unexpectedly stops (defensive coding)

struct WindowData {
  std::shared_ptr<encfs::FsIO> native_fs;
  std::vector<lockbox::win::MountDetails> mounts;
  bool is_stopping;
  UINT TASKBAR_CREATED_MSG;
  UINT LOCKBOX_DUPLICATE_INSTANCE_MSG;
  HWND last_foreground_window;
  bool popup_menu_is_open;
  lockbox::win::ManagedMenuHandle tray_menu;
  bool control_was_pressed_on_tray_open;
  lockbox::RecentlyUsedPathStoreV1 recent_mount_paths_store;
  encfs::Path first_run_cookie_path;
  unsigned timer_rev;
};

// constants
const wchar_t MAIN_WINDOW_CLASS_NAME[] = L"lockbox_tray_icon";
const UINT_PTR STOP_RELEVANT_DRIVE_THREADS_TIMER_ID = 0;

const auto APP_BASE = (UINT) (6 + WM_APP);
const auto LOCKBOX_TRAY_ICON_MSG = APP_BASE + 1;
const auto LOCKBOX_TRAY_ICON_MSG_2 = APP_BASE + 2;
const auto LOCKBOX_TRAY_ICON_ID = 1;

const wchar_t LOCKBOX_SINGLE_APP_INSTANCE_MUTEX_NAME[] =
  L"LockboxAppMutex";
const wchar_t LOCKBOX_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME[] =
  L"LockboxAppSharedProcessId";
const wchar_t LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME[] =
  L"LOCKBOX_DUPLICATE_INSTANCE_MESSAGE_NAME";
const wchar_t TASKBAR_CREATED_MESSAGE_NAME[] =
  L"TaskbarCreated";

static
void
bubble_msg(HWND lockbox_main_window,
           const std::string & title, const std::string & msg);

static
void
update_tray_menu(WindowData & wd);

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

  update_tray_menu(wd);

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
  for (auto it = wd.mounts.begin(); it != wd.mounts.end();) {
    if (!it->is_still_mounted()) {
      it = stop_drive(lockbox_main_window, wd, it);
    }
    else ++it;
  }
}

static
void
unmount_and_stop_drive(HWND hwnd, WindowData & wd, size_t mount_idx) {
  auto mount_p = wd.mounts.begin() + mount_idx;

  // first unmount drive
  mount_p->unmount();

  // now remove mount from list
  stop_drive(hwnd, wd, mount_p);
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

static
void
new_mount(WindowData & wd, lockbox::win::MountDetails md) {
  wd.recent_mount_paths_store.use_path(md.get_source_path());
  wd.mounts.push_back(std::move(md));
  update_tray_menu(wd);
  try { wd.mounts.back().open_mount(); }
  catch (const std::exception & err) {
    lbx_log_error("Error opening %s: %s",
                  std::to_string(md.get_drive_letter()).c_str(),
                  err.what());
  }
}

static
bool
run_create_dialog(HWND lockbox_main_window, WindowData & wd) {
  // only run this dialog if we don't already have a dialog
  assert(!GetWindow(lockbox_main_window, GW_ENABLEDPOPUP));

  auto ret = lockbox::win::create_new_lockbox_dialog(lockbox_main_window, wd.native_fs);
  if (ret) new_mount(wd, std::move(*ret));
  return (bool) ret;
}

static
bool
run_mount_dialog(HWND lockbox_main_window, WindowData & wd,
                 opt::optional<encfs::Path> p = opt::nullopt) {
  // only run this dialog if we don't already have a dialog
  assert(!GetWindow(lockbox_main_window, GW_ENABLEDPOPUP));

  lockbox::win::TakeMountFn take_mount = [&] (const encfs::Path & encrypted_container_path) -> opt::optional<lockbox::win::MountDetails> {
    auto it = std::find_if(wd.mounts.begin(), wd.mounts.end(),
                           [&] (const lockbox::win::MountDetails & md) {
                             return md.get_source_path() == encrypted_container_path;
                           });
    if (it == wd.mounts.end()) return opt::nullopt;

    auto md = std::move(*it);
    wd.mounts.erase(it);
    return std::move(md);
  };

  auto ret = lockbox::win::mount_existing_lockbox_dialog(lockbox_main_window,
                                                         wd.native_fs,
                                                         take_mount, p);
  if (ret) new_mount(wd, std::move(*ret));
  return (bool) ret;
}

static
bool
run_quit_confirmation_dialog(HWND lockbox_main_window) {
  auto ret = MessageBoxW(lockbox_main_window,
                         w32util::widen(LOCKBOX_DIALOG_QUIT_CONFIRMATION_MESSAGE).c_str(),
                         w32util::widen(LOCKBOX_DIALOG_QUIT_CONFIRMATION_TITLE).c_str(),
                         MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND);
  return ret == IDYES;
}

template<size_t N>
void
copy_to_wide_buffer(wchar_t (&dest)[N], const std::string & src) {
  auto wsrc = w32util::widen(src);
  if (wsrc.size() >= N) throw std::runtime_error("src is too long!");
  wmemcpy(dest, wsrc.c_str(), wsrc.size() + 1);
}

static
void
bubble_msg(HWND lockbox_main_window,
           const std::string & title, const std::string & msg) {
  NOTIFYICONDATAW icon_data;
  lockbox::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = lockbox_main_window;
  icon_data.uID = LOCKBOX_TRAY_ICON_ID;
  icon_data.uFlags = NIF_INFO;
  icon_data.uVersion = NOTIFYICON_VERSION;
  copy_to_wide_buffer(icon_data.szInfo, msg);
  icon_data.uTimeout = 0; // let it be the minimum
  copy_to_wide_buffer(icon_data.szInfoTitle, title);
  icon_data.dwInfoFlags = NIIF_NONE;

  auto success = Shell_NotifyIconW(NIM_MODIFY, &icon_data);
  if (!success) throw w32util::windows_error();
}

static
void
update_tray_menu(WindowData & wd) {
  w32util::menu_clear(wd.tray_menu.get());
  auto tm = lockbox::win::TrayMenu(wd.tray_menu);
  lockbox::populate_tray_menu(tm,
                              wd.mounts,
                              wd.recent_mount_paths_store,
                              wd.control_was_pressed_on_tray_open);
  auto success = SetMenuDefaultItem(wd.tray_menu.get(), 0, TRUE);
  if (!success) throw w32util::windows_error();
}

static
void
dispatch_tray_menu_action(HWND lockbox_main_window, WindowData & wd, UINT selected) {
  using lockbox::TrayMenuAction;
  TrayMenuAction menu_action;
  lockbox::tray_menu_action_arg_t menu_action_arg;

  std::tie(menu_action, menu_action_arg) = lockbox::decode_menu_id(selected);

  switch (menu_action) {
  case TrayMenuAction::CREATE: {
    run_create_dialog(lockbox_main_window, wd);
    break;
  }
  case TrayMenuAction::MOUNT: {
    run_mount_dialog(lockbox_main_window, wd);
    break;
  }
  case TrayMenuAction::ABOUT_APP: {
    lockbox::win::about_dialog(lockbox_main_window);
    break;
  }
  case TrayMenuAction::TRIGGER_BREAKPOINT: {
    DebugBreak();
    break;
  }
  case TrayMenuAction::TEST_BUBBLE: {
    bubble_msg(lockbox_main_window, LOCKBOX_NOTIFICATION_TEST_TITLE,
               LOCKBOX_NOTIFICATION_TEST_MESSAGE);
    break;
  }
  case TrayMenuAction::QUIT_APP: {
    const bool actually_quit = wd.mounts.empty()
      ? true
      : run_quit_confirmation_dialog(lockbox_main_window);
    if (actually_quit) DestroyWindow(lockbox_main_window);
    break;
  }
  case TrayMenuAction::OPEN: {
    wd.mounts[menu_action_arg].open_mount();
    break;
  }
  case TrayMenuAction::UNMOUNT: {
    unmount_and_stop_drive(lockbox_main_window, wd, menu_action_arg);
    break;
  }
  case TrayMenuAction::CLEAR_RECENTS: {
    wd.recent_mount_paths_store.clear();
    update_tray_menu(wd);
    break;
  }
  case TrayMenuAction::MOUNT_RECENT: {
    auto path = wd.recent_mount_paths_store.recently_used_paths()[menu_action_arg];
    run_mount_dialog(lockbox_main_window, wd, path);
    break;
  }
  default: {
    /* should never happen */
    assert(false);
    lbx_log_warning("Bad tray action: %d", (int) menu_action);
  }
  }
}

static
void
perform_default_tray_action(HWND lockbox_main_window, WindowData & wd) {
  assert(!wd.popup_menu_is_open);
  auto child = alert_of_popup_if_we_have_one(lockbox_main_window, wd, false);
  if (child) return;

  wd.control_was_pressed_on_tray_open = false;
  update_tray_menu(wd);

  auto default_menu_item_id =
    GetMenuDefaultItem(wd.tray_menu.get(), FALSE, GMDI_GOINTOPOPUPS);
  if (default_menu_item_id == (decltype(default_menu_item_id)) -1) throw w32util::windows_error();

  dispatch_tray_menu_action(lockbox_main_window, wd,
                            (UINT) default_menu_item_id);
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
  icon_data.hIcon = (HICON) LoadImageW(GetModuleHandle(NULL), IDI_LBX_APP,
                                       IMAGE_ICON, 16, 16, 0);
  icon_data.uVersion = NOTIFYICON_VERSION;
  copy_to_wide_buffer(icon_data.szTip, LOCKBOX_TRAY_ICON_TOOLTIP);

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

static
bool
is_first_run(const WindowData & wd) {
  return !encfs::file_exists(wd.native_fs, wd.first_run_cookie_path);
}

static
void
record_app_start(WindowData & wd) {
  // TODO: we can run this async
  bool open_for_write = true;
  bool should_create = true;
  wd.native_fs->openfile(wd.first_run_cookie_path,
                         open_for_write, should_create);
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
    NO_FALLTHROUGH(WM_TIMER): {
      if (wParam == STOP_RELEVANT_DRIVE_THREADS_TIMER_ID) {
        auto local_timer_rev = wd->timer_rev;
        if (!wd->is_stopping) {
          wd->is_stopping = true;
          // if this throws an exception, our app is going to close
          stop_relevant_drive_threads(hwnd, *wd);
          wd->is_stopping = false;
        }

        // if the timer rev changes, that means the timer was re-triggered
        // while we were stopping the drives so don't kill it
        if (local_timer_rev == wd->timer_rev) {
          KillTimer(hwnd, STOP_RELEVANT_DRIVE_THREADS_TIMER_ID);
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
        wd->timer_rev += 1;
      }
      return TRUE;
    }

    NO_FALLTHROUGH(WM_CREATE): {
      // set application state (WindowData) in main window
      const auto wd = (WindowData *) ((LPCREATESTRUCT) lParam)->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) wd);

      auto first_run = is_first_run(*wd);

      add_tray_icon(hwnd);

      // Set app icons (window icon and alt+tab icon)
      // NB: we only need to set this on top-level windows,
      //     dialogs that use this window as a parent derive
      //     this property
      SendMessage(hwnd, WM_SETICON,
                  ICON_SMALL,
                  (LPARAM) LoadImageW(GetModuleHandle(NULL), IDI_LBX_APP,
                                      IMAGE_ICON, 16, 16, LR_SHARED));
      SendMessage(hwnd, WM_SETICON,
                  ICON_BIG,
                  (LPARAM) LoadImageW(GetModuleHandle(NULL), IDI_LBX_APP,
                                      IMAGE_ICON, 32, 32, LR_SHARED));

      auto choice =
        lockbox::win::WelcomeDialogChoice::NOTHING;
      if (first_run) choice = lockbox::win::welcome_dialog(hwnd);

      record_app_start(*wd);

      auto should_bubble = true;
      // NB: if any of the initial actions were canceled,
      //     then we should still do an introductory bubble
      if (choice == lockbox::win::WelcomeDialogChoice::CREATE_NEW_LOCKBOX) {
        should_bubble = !run_create_dialog(hwnd, *wd);
      }
      else if (choice == lockbox::win::WelcomeDialogChoice::MOUNT_EXISTING_LOCKBOX) {
        should_bubble = !run_mount_dialog(hwnd, *wd);
      }
      else if (!wd->recent_mount_paths_store.empty()) {
        assert(!first_run);
        // if the user has mounted paths in the past
        // auto start the mount dialog with the most recently path
        // populated in the ui
        should_bubble = run_mount_dialog(hwnd, *wd,
                                         wd->recent_mount_paths_store.front());
      }

      if (should_bubble) {
        bubble_msg(hwnd,
                   LOCKBOX_TRAY_ICON_WELCOME_TITLE,
                   LOCKBOX_TRAY_ICON_WELCOME_MSG);
      }

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
          perform_default_tray_action(hwnd, *wd);
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

      return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    NO_FALLTHROUGH(LOCKBOX_TRAY_ICON_MSG_2): {
      // only do the action if we don't have a dialog up
      auto child = alert_of_popup_if_we_have_one(hwnd, *wd, false);
      if (child) return 0;

      if (wd->popup_menu_is_open) return 0;

      try {
        wd->control_was_pressed_on_tray_open = (bool) wParam;
        update_tray_menu(*wd);

        // NB: have to do this so the menu disappears
        // when you click out of it
        SetForegroundWindow(hwnd);

        wd->popup_menu_is_open = true;
        SetLastError(0);
        auto selected =
          (UINT) TrackPopupMenu(wd->tray_menu.get(),
                                TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                                TPM_RIGHTBUTTON |
                                TPM_NONOTIFY | TPM_RETURNCMD,
                                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                0, hwnd, NULL);
        wd->popup_menu_is_open = false;

        if (!selected && GetLastError()) throw std::runtime_error("TrackPopupMenu");

        // according to MSDN, we must force a "task switch"
        // this is so that if the user attempts to open the
        // menu while the menu is open, it reopens instead
        // of disappearing
        PostMessage(hwnd, WM_NULL, 0, 0);

        if (selected) dispatch_tray_menu_action(hwnd, *wd, selected);
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
    else if (msg == LOCKBOX_DUPLICATE_INSTANCE_MSG) {
      // do the current default action
      perform_default_tray_action(hwnd, *wd);
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

  auto tray_menu_handle = CreatePopupMenu();
  if (!tray_menu_handle) throw w32util::windows_error();

  wchar_t app_directory_buf[MAX_PATH];
  auto result = SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                                 NULL, SHGFP_TYPE_CURRENT, app_directory_buf);
  if (result != S_OK) throw w32util::windows_error();
  auto app_directory = w32util::narrow(app_directory_buf, wcslen(app_directory_buf));
  auto app_directory_path = native_fs->pathFromString(app_directory).join(PRODUCT_NAME_A);

  // make `app_directory_path` if it doesn't already exists
  try { native_fs->mkdir(app_directory_path); }
  catch (const std::system_error & err) {
    if (err.code() != std::errc::file_exists) throw;
  }

  auto recently_used_paths_storage_path =
    app_directory_path.join(LOCKBOX_RECENTLY_USED_PATHS_V1_FILE_NAME);
  auto path_store =
    lockbox::RecentlyUsedPathStoreV1(native_fs,
                                     recently_used_paths_storage_path,
                                     LOCKBOX_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);

  auto first_run_cookie_path = app_directory_path.join(LOCKBOX_APP_STARTED_COOKIE_FILENAME);

  auto wd = WindowData {
    /*.native_fs = */std::move(native_fs),
    /*.mounts = */std::vector<lockbox::win::MountDetails>(),
    /*.is_stopping = */false,
    /*.TASKBAR_CREATED_MSG = */TASKBAR_CREATED_MSG,
    /*.LOCKBOX_DUPLICATE_INSTANCE_MSG = */LOCKBOX_DUPLICATE_INSTANCE_MSG,
    /*.last_foreground_window = */NULL,
    /*.popup_menu_is_open = */false,
    /*.tray_menu = */lockbox::win::ManagedMenuHandle(tray_menu_handle),
    /*.control_was_pressed_on_tray_open = */false,
    /*.recent_mount_paths_store = */std::move(path_store),
    /*.first_run_cookie_path = */std::move(first_run_cookie_path),
    /*.timer_rev = */0,
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

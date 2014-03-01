/*
  Safe: Encrypted File System
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

#include <safe/win/about_dialog.hpp>
#include <safe/win/create_safe_dialog.hpp>
#include <safe/constants.h>
#include <safe/fs.hpp>
#include <safe/logging.h>
#include <safe/win/mount_safe_dialog.hpp>
#include <safe/win/mount.hpp>
#include <safe/parse.hpp>
#include <safe/win/ramdisk.hpp>
#include <safe/recent_paths_storage.hpp>
#include <safe/win/resources.h>
#include <safe/win/system_changes_dialog.hpp>
#include <safe/tray_menu.hpp>
#include <safe/win/tray_menu.hpp>
#include <safe/webdav_server.hpp>
#include <safe/win/welcome_dialog.hpp>
#include <safe/win/report_bug_dialog.hpp>
#include <safe/win/general_safe_dialog.hpp>
#include <w32util/async.hpp>
#include <w32util/file.hpp>
#include <w32util/gui_util.hpp>
#include <w32util/menu.hpp>
#include <w32util/shell.hpp>
#include <w32util/string.hpp>
#include <safe/util.hpp>
#include <safe/report_exception.hpp>

#include <encfs/base/logging.h>
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
#include <commctrl.h>
#include <shellapi.h>
#include <dbt.h>
#include <shlobj.h>
#include <powrprof.h>

#ifndef GW_ENABLEDPOPUP
#define GW_ENABLEDPOPUP 6
#endif

// TODO:
// 3) Unmount when webdav server unexpectedly stops (defensive coding)

struct WindowData {
  std::shared_ptr<encfs::FsIO> native_fs;
  std::vector<safe::win::MountDetails> mounts;
  bool is_stopping;
  UINT TASKBAR_CREATED_MSG;
  UINT SAFE_DUPLICATE_INSTANCE_MSG;
  HWND last_foreground_window;
  bool popup_menu_is_open;
  safe::win::ManagedMenuHandle tray_menu;
  bool control_was_pressed_on_tray_open;
  safe::RecentlyUsedPathStoreV1 recent_mount_paths_store;
  encfs::Path first_run_cookie_path;
  unsigned timer_rev;
};

// constants
const wchar_t MAIN_WINDOW_CLASS_NAME[] = L"safe_tray_icon";
const UINT_PTR STOP_RELEVANT_DRIVE_THREADS_TIMER_ID = 0;

const auto APP_BASE = (UINT) (6 + WM_APP);
const auto SAFE_TRAY_ICON_MSG = APP_BASE + 1;
const auto SAFE_TRAY_ICON_MSG_2 = APP_BASE + 2;
const auto SAFE_TRAY_ICON_ID = 1;

const wchar_t SAFE_SINGLE_APP_INSTANCE_MUTEX_NAME[] =
  L"SafeAppMutex";
const wchar_t SAFE_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME[] =
  L"SafeAppSharedProcessId";
const wchar_t SAFE_DUPLICATE_INSTANCE_MESSAGE_NAME[] =
  L"SAFE_DUPLICATE_INSTANCE_MESSAGE_NAME";
const wchar_t TASKBAR_CREATED_MESSAGE_NAME[] =
  L"TaskbarCreated";
const wchar_t MAKE_REQUIRED_SYSTEM_CHANGES_ARG_W[] =
  L"/make_required_system_changes";

static
void
bubble_msg(HWND safe_main_window,
           const std::string & title, const std::string & msg);

static
void
update_tray_menu(WindowData & wd);

static
void
remove_mount_from_favorites(const safe::win::MountDetails & mount);

static
std::vector<safe::win::MountDetails>::iterator
stop_drive(HWND safe_main_window,
           WindowData & wd,
           std::vector<safe::win::MountDetails>::iterator it) {
  try {
    remove_mount_from_favorites(*it);
  }
  catch (const std::exception & err) {
    // this is fine, only works on vista anyway
    // TODO: only ignore this error if we dont' have
    // this functionality on this system
    lbx_log_error("error while removing mount from favorites: %s",
                  err.what());
  }

  // remove mount from list (even if thread hasn't died)
  auto md = std::move(*it);
  it = wd.mounts.erase(it);

  update_tray_menu(wd);

  // TODO: this might be too spammy if multiple drives have been
  // unmounted
  bubble_msg(safe_main_window,
             "Success",
             "You've successfully stopped \"" + md.get_mount_name() +
             ".\"");

  return it;
}

static
void
stop_relevant_drive_threads(HWND safe_main_window, WindowData & wd) {
  for (auto it = wd.mounts.begin(); it != wd.mounts.end();) {
    if (!it->is_still_mounted()) {
      it = stop_drive(safe_main_window, wd, it);
    }
    else ++it;
  }
}

static
std::string
get_links_folder_path() {
  // We dynamically get a reference to "SHGetKnownFolderPath"
  // this is to keep runtime compatibilty with Windows XP
  HMODULE mod;
  w32util::check_bool(GetModuleHandleExW,
                      0, L"shell32.dll", &mod);
  auto _free_mod = safe::create_deferred(FreeLibrary, mod);

  auto func = w32util::check_null(GetProcAddress,
                                  mod, "SHGetKnownFolderPath");

  typedef HRESULT (WINAPI *SHGetKnownFolderPathType)(const GUID *,
                                                     DWORD,
                                                     HANDLE,
                                                     PWSTR *);

  auto SHGetKnownFolderPath = (SHGetKnownFolderPathType) func;

  const GUID LINKS_GUID = {0xbfb9d5e0, 0xc6a9, 0x404c,
                           {0xb2, 0xb2,
                            0xae, 0x6d, 0xb6, 0xaf, 0x49, 0x68}};

  const DWORD KF_FLAG_CREATE = 0x00008000;

  PWSTR out;
  w32util::check_hresult(SHGetKnownFolderPath,
                         &LINKS_GUID, KF_FLAG_CREATE,
                         nullptr, &out);
  auto _free_out = safe::create_deferred(CoTaskMemFree, (void *) out);

  return w32util::narrow(out);
}

static
std::string
get_mount_shortcut_path(const safe::win::MountDetails & mount) {
  auto links_folder_path = get_links_folder_path();
  auto shortcut_path =
    (links_folder_path + "\\" + mount.get_mount_name() +
     " (Safe Mount).lnk");
  return shortcut_path;
}

static
void
add_mount_to_favorites(const safe::win::MountDetails & mount) {
  auto f = get_mount_shortcut_path(mount);
  lbx_log_debug("creating shortcut at %s", f.c_str());
  w32util::create_shortcut(get_mount_shortcut_path(mount),
                           std::to_string(mount.get_drive_letter()) + ":\\");
}

static
void
remove_mount_from_favorites(const safe::win::MountDetails & mount) {
  w32util::ensure_deleted(get_mount_shortcut_path(mount));
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
alert_of_popup_if_we_have_one(HWND safe_main_window, const WindowData & wd,
                              bool beep = true) {
  auto child = GetWindow(safe_main_window, GW_ENABLEDPOPUP);
  if (child) {
    SetForegroundWindow(child);

    if (beep && child == wd.last_foreground_window) {
      DWORD flash_count;
      auto success =
        SystemParametersInfoW(SPI_GETFOREGROUNDFLASHCOUNT,
                              0, &flash_count, 0);
      if (!success) w32util::throw_windows_error();

      FLASHWINFO finfo;
      safe::zero_object(finfo);
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
new_mount(WindowData & wd, safe::win::MountDetails md) {
  wd.recent_mount_paths_store.use_path(md.get_source_path());
  wd.mounts.push_back(std::move(md));
  update_tray_menu(wd);
  try {
    add_mount_to_favorites(wd.mounts.back());
  }
  catch (const std::exception & err) {
    // this is fine, only works on vista anyway
    lbx_log_error("error while adding mount to favorites: %s",
                  err.what());
  }
  try { wd.mounts.back().open_mount(); }
  catch (const std::exception & err) {
    lbx_log_error("Error opening %s: %s",
                  std::to_string(md.get_drive_letter()).c_str(),
                  err.what());
  }
}

static
bool
run_create_dialog(HWND safe_main_window, WindowData & wd) {
  // only run this dialog if we don't already have a dialog
  assert(!GetWindow(safe_main_window, GW_ENABLEDPOPUP));

  auto ret = safe::win::create_new_safe_dialog(safe_main_window, wd.native_fs);
  if (ret) new_mount(wd, std::move(*ret));
  return (bool) ret;
}

static
bool
run_mount_dialog(HWND safe_main_window, WindowData & wd,
                 opt::optional<encfs::Path> p = opt::nullopt) {
  // only run this dialog if we don't already have a dialog
  assert(!GetWindow(safe_main_window, GW_ENABLEDPOPUP));

  safe::win::TakeMountFn take_mount = [&] (const encfs::Path & encrypted_container_path) -> opt::optional<safe::win::MountDetails> {
    auto it = std::find_if(wd.mounts.begin(), wd.mounts.end(),
                           [&] (const safe::win::MountDetails & md) {
                             return md.get_source_path() == encrypted_container_path;
                           });
    if (it == wd.mounts.end()) return opt::nullopt;

    auto md = std::move(*it);
    wd.mounts.erase(it);
    return std::move(md);
  };

  safe::win::HaveMountFn have_mount = [&] (const encfs::Path & encrypted_container_path) {
    auto it = std::find_if(wd.mounts.begin(), wd.mounts.end(),
                           [&] (const safe::win::MountDetails & md) {
                             return md.get_source_path() == encrypted_container_path;
                           });
    return (it != wd.mounts.end());
  };

  auto ret = safe::win::mount_existing_safe_dialog(safe_main_window,
                                                   wd.native_fs,
                                                   take_mount,
                                                   have_mount,
                                                   p);
  if (ret) new_mount(wd, std::move(*ret));
  return (bool) ret;
}

static
bool
run_quit_confirmation_dialog(HWND safe_main_window) {
  auto ret = MessageBoxW(safe_main_window,
                         w32util::widen(SAFE_DIALOG_QUIT_CONFIRMATION_MESSAGE).c_str(),
                         w32util::widen(SAFE_DIALOG_QUIT_CONFIRMATION_TITLE).c_str(),
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
bubble_msg(HWND safe_main_window,
           const std::string & title, const std::string & msg) {
  NOTIFYICONDATAW icon_data;
  safe::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = safe_main_window;
  icon_data.uID = SAFE_TRAY_ICON_ID;
  icon_data.uFlags = NIF_INFO;
  icon_data.uVersion = NOTIFYICON_VERSION;
  copy_to_wide_buffer(icon_data.szInfo, msg);
  icon_data.uTimeout = 0; // let it be the minimum
  copy_to_wide_buffer(icon_data.szInfoTitle, title);
  icon_data.dwInfoFlags = NIIF_NONE;

  auto success = Shell_NotifyIconW(NIM_MODIFY, &icon_data);
  if (!success) w32util::throw_windows_error();
}

static
std::string
get_application_path() {
  WCHAR application_path[MAX_PATH + 1];
  const auto num_chars = safe::numelementsf(application_path);
  auto ret = GetModuleFileName(nullptr, application_path, num_chars);
  if (!ret ||
      (num_chars == ret &&
       GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
    w32util::throw_windows_error();
  }
  return w32util::narrow(application_path);
}

static
std::string
get_shortcut_path() {
  auto startup_directory =
    w32util::get_folder_path(CSIDL_STARTUP | CSIDL_FLAG_CREATE,
                             SHGFP_TYPE_CURRENT);
  return (startup_directory + "\\" +
          PRODUCT_NAME_A + ".lnk");
}

static
bool
app_is_run_at_login() {
  /* return true for now */
  auto shortcut_path = get_shortcut_path();
  auto application_path = get_application_path();

  try {
    auto shortcut_target = w32util::get_shortcut_target(shortcut_path);
    return w32util::map_to_same_target(shortcut_target, application_path);
  }
  catch (const std::system_error & err) {
    if (err.code() == std::errc::no_such_file_or_directory) {
      return false;
    }
    throw;
  }
}

static
void
set_app_to_run_at_login(bool run_at_login) {
  if (run_at_login) {
    w32util::create_shortcut(get_shortcut_path(),
                             get_application_path());
  }
  else {
    w32util::ensure_deleted(get_shortcut_path());
  }
}

static
bool
set_highest_non_disabled_to_default(HMENU root) {
  // do a basic recursive dfs, it should be okay since we trust
  // our menus

  auto num_items = w32util::check_call(-1, GetMenuItemCount, root);
  for (UINT i = 0; i < (UINT) num_items; ++i) {
    auto set_default = false;
    auto submenu = GetSubMenu(root, i);
    if (submenu) {
      set_default = set_highest_non_disabled_to_default(submenu);
    }
    else {
      auto state = GetMenuState(root, i, MF_BYPOSITION);
      set_default = !(state & MF_DISABLED);
    }

    if (set_default) {
      w32util::check_bool(SetMenuDefaultItem, root, i, TRUE);
      return true;
    }
  }

  return false;
}

static
void
update_tray_menu(WindowData & wd) {
  w32util::menu_clear(wd.tray_menu.get());
  auto tm = safe::win::TrayMenu(wd.tray_menu);

  // NB: don't allow more than one mount on windows xp.
  //     xp can only mount drives on port 80 and
  //     currently we listen on a different port for
  //     each drive mounted
  bool allow_more_mounts = (!safe::win::running_on_winxp() ||
                            wd.mounts.size() < 1);

  safe::populate_tray_menu(tm,
                           wd.mounts,
                           wd.recent_mount_paths_store,
                           app_is_run_at_login(),
                           allow_more_mounts,
                           wd.control_was_pressed_on_tray_open);

  auto ret = set_highest_non_disabled_to_default(wd.tray_menu.get());
  assert(ret);
  (void) ret;
}

static
void
dispatch_tray_menu_action(HWND safe_main_window, WindowData & wd, UINT selected) {
  using safe::TrayMenuAction;
  TrayMenuAction menu_action;
  safe::tray_menu_action_arg_t menu_action_arg;

  std::tie(menu_action, menu_action_arg) = safe::decode_menu_id(selected);

  switch (menu_action) {
  case TrayMenuAction::CREATE: {
    run_create_dialog(safe_main_window, wd);
    break;
  }
  case TrayMenuAction::MOUNT: {
    run_mount_dialog(safe_main_window, wd);
    break;
  }
  case TrayMenuAction::TOGGLE_RUN_AT_LOGIN: {
    set_app_to_run_at_login(!app_is_run_at_login());
    break;
  }
  case TrayMenuAction::ABOUT_APP: {
    safe::win::about_dialog(safe_main_window);
    break;
  }
  case TrayMenuAction::TRIGGER_BREAKPOINT: {
    DebugBreak();
    break;
  }
  case TrayMenuAction::TEST_BUBBLE: {
    bubble_msg(safe_main_window, SAFE_NOTIFICATION_TEST_TITLE,
               SAFE_NOTIFICATION_TEST_MESSAGE);
    break;
  }
  case TrayMenuAction::QUIT_APP: {
    const bool actually_quit = wd.mounts.empty()
      ? true
      : run_quit_confirmation_dialog(safe_main_window);
    if (actually_quit) DestroyWindow(safe_main_window);
    break;
  }
  case TrayMenuAction::OPEN: {
    wd.mounts[menu_action_arg].open_mount();
    break;
  }
  case TrayMenuAction::UNMOUNT: {
    unmount_and_stop_drive(safe_main_window, wd, menu_action_arg);
    break;
  }
  case TrayMenuAction::CLEAR_RECENTS: {
    wd.recent_mount_paths_store.clear();
    update_tray_menu(wd);
    break;
  }
  case TrayMenuAction::MOUNT_RECENT: {
    auto path = std::get<0>(wd.recent_mount_paths_store[menu_action_arg].resolve_path());
    run_mount_dialog(safe_main_window, wd, path);
    break;
  }
  case TrayMenuAction::SEND_FEEDBACK: {
    w32util::open_url_in_browser(safe_main_window, SAFE_SEND_FEEDBACK_WEBSITE);
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
perform_default_tray_action(HWND safe_main_window, WindowData & wd) {
  assert(!wd.popup_menu_is_open);
  auto child = alert_of_popup_if_we_have_one(safe_main_window, wd, false);
  if (child) return;

  SetForegroundWindow(safe_main_window);

  wd.control_was_pressed_on_tray_open = false;
  update_tray_menu(wd);

  auto default_menu_item_id =
    GetMenuDefaultItem(wd.tray_menu.get(), FALSE, GMDI_GOINTOPOPUPS);
  if (default_menu_item_id == (decltype(default_menu_item_id)) -1) w32util::throw_windows_error();

  dispatch_tray_menu_action(safe_main_window, wd,
                            (UINT) default_menu_item_id);
}

static
void
add_tray_icon(HWND safe_main_window) {
  NOTIFYICONDATAW icon_data;
  safe::zero_object(icon_data);
  icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
  icon_data.hWnd = safe_main_window;
  icon_data.uID = SAFE_TRAY_ICON_ID;
  icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon_data.uCallbackMessage = SAFE_TRAY_ICON_MSG;
  icon_data.hIcon = (HICON) LoadImageW(GetModuleHandle(NULL), IDI_SFX_APP,
                                       IMAGE_ICON, 16, 16, LR_SHARED);
  icon_data.uVersion = NOTIFYICON_VERSION;
  copy_to_wide_buffer(icon_data.szTip, SAFE_TRAY_ICON_TOOLTIP);

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
  return (!encfs::file_exists(wd.native_fs, wd.first_run_cookie_path) &&
          wd.recent_mount_paths_store.empty());
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

static
bool
is_app_running_as_admin() {
  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
  PSID administrators_group = nullptr;
  auto success = AllocateAndInitializeSid(&nt_authority,
                                          2, 
                                          SECURITY_BUILTIN_DOMAIN_RID, 
                                          DOMAIN_ALIAS_RID_ADMINS, 
                                          0, 0, 0, 0, 0, 0, 
                                          &administrators_group);
  if (!success) w32util::throw_windows_error();

  auto _free_administrators_group_sid =
    safe::create_deferred(FreeSid, administrators_group);

  BOOL is_running_as_admin = FALSE;
  auto success2 = CheckTokenMembership(nullptr,
                                       administrators_group,
                                       &is_running_as_admin);
  if (!success2) w32util::throw_windows_error();

  return is_running_as_admin;
}

static
bool
hibernate_is_enabled() {
  SYSTEM_POWER_CAPABILITIES powercap;
  w32util::check_bool(GetPwrCapabilities, &powercap);
  return powercap.SystemS4 && powercap.HiberFilePresent;
}

static
bool
set_hibernate(bool hibernate) {
  auto ret = safe::win::run_command_sync("C:\\Windows\\system32\\powercfg.exe",
                                            hibernate
                                            ? "/hibernate on"
                                            : "/hibernate off");
  if (ret != EXIT_SUCCESS) throw std::runtime_error("powercfg failed");
  return false;
}

static
bool
encrypted_pagefile_is_enabled() {
  // create a temp file to store the result of
  // calling query
  auto temp_root_path = w32util::get_temp_path();
  auto temp_file_path = w32util::get_temp_file_name(temp_root_path,
                                                    "SAFETEMP");

  lbx_log_debug("Storing fsutil tempfile at %s",
                temp_file_path.c_str());

  SECURITY_ATTRIBUTES saAttr;
  safe::zero_object(saAttr);
  saAttr.nLength = sizeof(saAttr);
  saAttr.bInheritHandle = TRUE;

  auto hfile =
    w32util::check_invalid_handle(CreateFileW,
                                  w32util::widen(temp_file_path).c_str(),
                                  GENERIC_WRITE | GENERIC_READ,
                                  FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  &saAttr,
                                  OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL |
                                  FILE_FLAG_DELETE_ON_CLOSE,
                                  nullptr);
  auto _close_handle = safe::create_deferred(CloseHandle, hfile);

  // run process
  STARTUPINFO startup_info;
  safe::zero_object(startup_info);
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = hfile;
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  WCHAR cmd_line[] = L"fsutil behavior query EncryptPagingFile";

  PROCESS_INFORMATION pinfo;
  w32util::check_bool(CreateProcessW,
                      L"C:\\Windows\\system32\\fsutil.exe",
                      cmd_line,
                      nullptr,
                      &saAttr,
                      TRUE,
                      CREATE_NO_WINDOW,
                      nullptr,
                      nullptr,
                      &startup_info,
                      &pinfo);
  auto _close_process = safe::create_deferred([&] () {
      CloseHandle(pinfo.hThread);
      CloseHandle(pinfo.hProcess);
    });

  // wait for process to complete
  w32util::check_call(WAIT_FAILED, WaitForSingleObject,
                      pinfo.hProcess, INFINITE);

  DWORD exit_code;
  w32util::check_bool(GetExitCodeProcess, pinfo.hProcess, &exit_code);
  if (exit_code != EXIT_SUCCESS) throw std::runtime_error("failed fsutil");

  // read data from output, a reasonable amount is 256 bytes
  w32util::check_call(INVALID_SET_FILE_POINTER, SetFilePointer,
                      hfile, 0, nullptr, FILE_BEGIN);
  auto buffer = w32util::read_file(hfile, 256);

  // parse data
  auto parser = safe::BufferParser(buffer.ptr.get(), buffer.size);
  parser.skip_byte(' ');
  auto key_name = parser.parse_string_until_byte(' ');
  if (key_name != "EncryptPagingFile") {
    throw std::runtime_error("invalid key name: " + key_name);
  }

  parser.skip_byte(' ');
  parser.expect('=');
  parser.skip_byte(' ');
  return parser.parse_ascii_integer<DWORD>();
}

static
bool
set_encrypted_pagefile(bool encrypted) {
  auto cmdline_prefix = std::string("behavior set EncryptPagingFile ");
  auto retcode =
    safe::win::run_command_sync("C:\\Windows\\system32\\fsutil.exe",
                                   cmdline_prefix +
                                   (encrypted ? "1" : "0"));
  if (retcode != EXIT_SUCCESS) {
    throw std::runtime_error("error setting encrypted pagefile");
  }
  // NB: we just hardcode a required restart
  // TODO: potentially query if the value changed, or read
  //       the output from fsutil
  return true;
}

static
bool
system_changes_are_required() {
  return (safe::win::need_to_install_kernel_driver() ||
          hibernate_is_enabled() ||
          (!safe::win::running_on_winxp() &&
           !encrypted_pagefile_is_enabled()));
}

static
bool
make_required_system_changes() {
  bool must_restart = false;

  if (hibernate_is_enabled() &&
      set_hibernate(false)) {
    must_restart = true;
  }

  if (!safe::win::running_on_winxp() &&
      !encrypted_pagefile_is_enabled() &&
      set_encrypted_pagefile(true)) {
    must_restart = true;
  }

  if (safe::win::need_to_install_kernel_driver() &&
      safe::win::install_kernel_driver()) {
    must_restart = true;
  }

  return must_restart;
}

static
bool
make_required_system_changes_as_admin(HWND hwnd) {
  if (is_app_running_as_admin()) {
    auto maybe_reboot_required =
      w32util::modal_call(hwnd,
                          SAFE_PROGRESS_SYSTEM_CHANGES_TITLE,
                          SAFE_PROGRESS_SYSTEM_CHANGES_MESSAGE,
                          make_required_system_changes);
    assert(maybe_reboot_required);
    return *maybe_reboot_required;
  }

  // we have to start ourselves with a special flag
  auto application_path_w = w32util::widen(get_application_path());

  SHELLEXECUTEINFOW shex;
  safe::zero_object(shex);
  shex.cbSize = sizeof(shex);
  shex.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
  shex.lpVerb = L"runas";
  shex.lpFile = application_path_w.c_str();
  shex.lpParameters = MAKE_REQUIRED_SYSTEM_CHANGES_ARG_W;
  shex.nShow = SW_SHOWNORMAL;

  lbx_log_debug("Executing admin child, our pid is: %u",
                (unsigned) GetCurrentProcessId());

  auto success = ShellExecuteExW(&shex);
  if (!success) w32util::throw_windows_error();

  if (!shex.hProcess) w32util::throw_windows_error();

  auto _close_process_handle =
    safe::create_deferred(CloseHandle, shex.hProcess);

  // NB: we must keep a window up while performing the driver install,
  //     otherwise we'll lose focus
  w32util::modal_until_object(hwnd,
                              SAFE_PROGRESS_SYSTEM_CHANGES_TITLE,
                              SAFE_PROGRESS_SYSTEM_CHANGES_MESSAGE,
                              shex.hProcess);

  DWORD exit_code;
  auto success2 = GetExitCodeProcess(shex.hProcess, &exit_code);
  if (!success2) w32util::throw_windows_error();

  switch (exit_code) {
  case ERROR_SUCCESS: return false;
  case ERROR_SUCCESS_REBOOT_REQUIRED: return true;
  default: throw w32util::windows_error(exit_code);
  }
}

static
void
enable_and_initiate_shutdown() {
  // first enable shutdown privilege
  HANDLE token;
  w32util::check_bool(OpenProcessToken, GetCurrentProcess(),
                      TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);

  TOKEN_PRIVILEGES tkp;
  w32util::check_bool(LookupPrivilegeValue, nullptr, SE_SHUTDOWN_NAME,
                      &tkp.Privileges[0].Luid);
  tkp.PrivilegeCount = 1;
  tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  w32util::check_bool(AdjustTokenPrivileges, token,
                      FALSE, &tkp, 0, nullptr, nullptr);
  if (GetLastError() != ERROR_SUCCESS) w32util::throw_windows_error();

  auto _reduce_privilege = safe::create_deferred([&] () {
      tkp.Privileges[0].Attributes = 0;
      auto success = AdjustTokenPrivileges(token, FALSE, &tkp, 0,
                                           nullptr, nullptr);
      if (!success) {
        lbx_log_error("Failed to disable SE_SHUTDOWN_NAME privilege");
      }
    });

  // kick off shutdown
  w32util::check_bool(InitiateSystemShutdownW,
                      nullptr, nullptr,
                      0, FALSE, TRUE);
}

static
HKEY
get_hklm_safe_key() {
  HKEY safe_key;
  w32util::check_good_call(ERROR_SUCCESS, RegCreateKeyExW,
                           HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\Safe",
                           0,
                           nullptr,
                           REG_OPTION_VOLATILE,
                           KEY_ALL_ACCESS,
                           nullptr,
                           &safe_key,
                           nullptr);
  return safe_key;
}

static
void
set_reboot_pending(bool reboot_pending) {
  auto safe_key = get_hklm_safe_key();
  auto _close_key = safe::create_deferred(RegCloseKey, safe_key);

  const wchar_t *REBOOT_PENDING_KEY_W = L"RebootPending";

  if (reboot_pending) {
    w32util::check_good_call(ERROR_SUCCESS, RegSetValueExW, safe_key,
                             REBOOT_PENDING_KEY_W,
                             0,
                             REG_NONE,
                             nullptr, 0);
  }
  else {
    try {
      w32util::check_good_call(ERROR_SUCCESS, RegDeleteValueW,
                               safe_key, REBOOT_PENDING_KEY_W);
    }
    catch (const std::system_error & err) {
      if (err.code() != std::errc::no_such_file_or_directory) throw;
    }
  }
}

static
bool
is_reboot_pending() {
  auto safe_key = get_hklm_safe_key();
  auto _close_key = safe::create_deferred(RegCloseKey, safe_key);

  auto ret = RegQueryValueEx(safe_key,
                             L"RebootPending", nullptr, nullptr,
                             nullptr, nullptr);
  switch (ret) {
  case ERROR_SUCCESS: return true;
  case ERROR_FILE_NOT_FOUND: return false;
  default: w32util::throw_windows_error();
  }
  /* notreached */
  assert(false);
  return false;
}

static
void
run_reboot_sequence(HWND hwnd) {
  auto ret =
    w32util::check_call(0, MessageBoxW, hwnd,
                        w32util::widen(SAFE_DIALOG_REBOOT_CONFIRMATION_MESSAGE).c_str(),
                        w32util::widen(SAFE_DIALOG_REBOOT_CONFIRMATION_TITLE).c_str(),
                        MB_OKCANCEL | MB_ICONWARNING | MB_SETFOREGROUND);

  set_reboot_pending(true);

  if (ret == IDOK) {
    enable_and_initiate_shutdown();
  }
  else assert(ret == IDCANCEL);
}

static
bool
run_winxp_warning_dialog(HWND hwnd) {
  std::string message =
    ("The security of " PRODUCT_NAME_A " is degraded while "
     "running under Windows XP. This is not a limitation of "
     PRODUCT_NAME_A " and most security products on Windows XP "
     "suffer from the same weakness."
     "\n\n"
     "If you expect that the only "
     "people who will ever have access to your physical hard "
     "disk are trusted, "
     "then this is not an issue. Otherwise we don't recommend "
     "running " PRODUCT_NAME_A " in this environment."
     );

  enum class XPWarningChoice {
    CONTINUE,
    MORE_INFO,
    QUIT,
  };

  auto more_info = [&] {
    w32util::open_url_in_browser(hwnd, SAFE_WINXP_MORE_INFO_WEBSITE);
    return opt::nullopt;
  };

  std::vector<safe::win::Choice<XPWarningChoice>> choices = {
    {"Continue", XPWarningChoice::CONTINUE},
    {"More Info", more_info},
    {"Quit", XPWarningChoice::QUIT},
  };

  auto ret =
    safe::win::general_safe_dialog<XPWarningChoice>(hwnd,
                                                    "Windows XP Warning",
                                                    message,
                                                    std::move(choices),
                                                    opt::nullopt,
                                                    safe::win::GeneralDialogIcon::NONE);

  return ret == XPWarningChoice::CONTINUE;
}

#define NO_FALLTHROUGH(c) assert(false); case c

static
LRESULT
CALLBACK
main_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const auto wd = (WindowData *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
  const auto TASKBAR_CREATED_MSG = wd ? wd->TASKBAR_CREATED_MSG : 0;
  const auto SAFE_DUPLICATE_INSTANCE_MSG =
    wd ? wd->SAFE_DUPLICATE_INSTANCE_MSG : 0;
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
        if (!success) w32util::throw_windows_error();
        wd->timer_rev += 1;
      }
      return TRUE;
    }

    NO_FALLTHROUGH(WM_CREATE): {
      // set application state (WindowData) in main window
      const auto wd = (WindowData *) ((LPCREATESTRUCT) lParam)->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) wd);

      auto first_run = is_first_run(*wd);

      if (first_run) { set_app_to_run_at_login(true); }

      add_tray_icon(hwnd);

      // Set app icons (window icon and alt+tab icon)
      // NB: we only need to set this on top-level windows,
      //     dialogs that use this window as a parent derive
      //     this property
      SendMessage(hwnd, WM_SETICON,
                  ICON_SMALL,
                  (LPARAM) LoadImageW(GetModuleHandle(NULL), IDI_SFX_APP,
                                      IMAGE_ICON, 16, 16, LR_SHARED));
      SendMessage(hwnd, WM_SETICON,
                  ICON_BIG,
                  (LPARAM) LoadImageW(GetModuleHandle(NULL), IDI_SFX_APP,
                                      IMAGE_ICON, 32, 32, LR_SHARED));

      if (safe::win::running_on_winxp()) {
        auto should_continue = run_winxp_warning_dialog(hwnd);
        if (!should_continue) {
          PostMessage(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
      }

      if (is_reboot_pending()) {
        run_reboot_sequence(hwnd);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }

      bool made_system_changes = false;
      if (system_changes_are_required()) {
	// We need to install the kernel driver
	// Thank user for starting program and let them know
	// we have to make some changes before starting the program
	auto choice = safe::win::system_changes_dialog(hwnd);
	if (choice == safe::win::SystemChangesChoice::QUIT) {
          PostMessage(hwnd, WM_CLOSE, 0, 0);
	  return 0;
	}

	assert(choice == safe::win::SystemChangesChoice::OK);

        bool must_restart = false;
        try {
          must_restart = make_required_system_changes_as_admin(hwnd);
        }
        catch (...) {
          // show "report bug" dialog and quit
          auto choice =
            safe::win::report_bug_dialog(hwnd, "An error occured while attempting to make changes to your system.");
          if (choice == safe::win::ReportBugDialogChoice::REPORT_BUG) {
            safe::report_exception(safe::ExceptionLocation::SYSTEM_CHANGES,
                                   std::current_exception());
          }

          PostMessage(hwnd, WM_CLOSE, 0, 0);
	  return 0;
        }

        if (must_restart) {
          run_reboot_sequence(hwnd);
          PostMessage(hwnd, WM_CLOSE, 0, 0);
	  return 0;
        }

	made_system_changes = true;
      }

      auto choice =
        safe::win::WelcomeDialogChoice::NOTHING;
      if (first_run) choice = safe::win::welcome_dialog(hwnd,
                                                           made_system_changes);

      record_app_start(*wd);

      auto should_bubble = true;
      // NB: if any of the initial actions were canceled,
      //     then we should still do an introductory bubble
      if (choice == safe::win::WelcomeDialogChoice::CREATE_NEW_SAFE) {
        should_bubble = !run_create_dialog(hwnd, *wd);
      }
      else if (choice == safe::win::WelcomeDialogChoice::MOUNT_EXISTING_SAFE) {
        should_bubble = !run_mount_dialog(hwnd, *wd);
      }
      else if (!wd->recent_mount_paths_store.empty()) {
        assert(!first_run);
        // if the user has mounted paths in the past
        // auto start the mount dialog with the most recently path
        // populated in the ui
        should_bubble = !run_mount_dialog(hwnd, *wd,
                                          std::get<0>(wd->recent_mount_paths_store.front().resolve_path()));
      }

      if (should_bubble) {
        bubble_msg(hwnd,
                   SAFE_TRAY_ICON_WELCOME_TITLE,
                   SAFE_TRAY_ICON_WELCOME_MSG);
      }

      // return -1 on failure, CreateWindow* will return NULL
      return 0;
    }

    NO_FALLTHROUGH(SAFE_TRAY_ICON_MSG): {
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

          PostMessage(hwnd, SAFE_TRAY_ICON_MSG_2,
                      (LPARAM) is_control_pressed,
                      MAKELPARAM(xypos.x, xypos.y));
          break;
        }

      default: break;
      }

      return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    NO_FALLTHROUGH(SAFE_TRAY_ICON_MSG_2): {
      // only do the action if we don't have a dialog up
      auto child = alert_of_popup_if_we_have_one(hwnd, *wd, false);
      if (child) return 0;

      if (wd->popup_menu_is_open) return 0;

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

      return 0;
    }

    NO_FALLTHROUGH(WM_CLOSE): {
      DestroyWindow(hwnd);
      return 0;
    }

    NO_FALLTHROUGH(WM_DESTROY): {
      // remove tray icon
      NOTIFYICONDATA icon_data;
      safe::zero_object(icon_data);
      icon_data.cbSize = NOTIFYICONDATA_V2_SIZE;
      icon_data.hWnd = hwnd;
      icon_data.uID = SAFE_TRAY_ICON_ID;
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
    else if (msg == SAFE_DUPLICATE_INSTANCE_MSG) {
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
    CreateMutexW(NULL, TRUE, SAFE_SINGLE_APP_INSTANCE_MUTEX_NAME);
  switch (GetLastError()) {
  case ERROR_SUCCESS: {
    // write our process id in a shared memory
    auto mapping_handle =
      CreateFileMapping(INVALID_HANDLE_VALUE,
                        NULL, PAGE_READWRITE,
                        0, sizeof(DWORD),
                        SAFE_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME);
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
                      SAFE_SINGLE_APP_INSTANCE_SHARED_MEMORY_NAME);
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

static
int
winmain_inner(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
              LPSTR lpCmdLine, int nCmdShow) {
  // TODO: de-initialize
  log_printer_default_init();
  encfs_set_log_printer(encfs_log_printer_adapter);

#ifdef NDEBUG
  logging_set_global_level(LOG_NOTHING);
  encfs_set_log_level(ENCFS_LOG_NOTHING);
#else
  logging_set_global_level(LOG_DEBUG);
  encfs_set_log_level(ENCFS_LOG_DEBUG);
#endif
  lbx_log_debug("Hello world! CmdLine: %s", lpCmdLine);

  bool make_required_system_changes_ = false;
  {
    int argc;
    auto wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) w32util::throw_windows_error();

    auto _free_wargv = safe::create_deferred(LocalFree, wargv);

    make_required_system_changes_ =
      (argc == 2 &&
       !wcscmp(wargv[1], MAKE_REQUIRED_SYSTEM_CHANGES_ARG_W));
  }

  if (make_required_system_changes_) {
    int toret;
    try {
      auto restart_required = make_required_system_changes();
      toret = restart_required
        ? ERROR_SUCCESS_REBOOT_REQUIRED
        : ERROR_SUCCESS;
    }
    catch (const w32util::windows_error & err) {
      lbx_log_error("Failure to make system changes: %s", err.what());
      toret = err.code().value();
    }
    catch (const std::exception & err) {
      lbx_log_error("Failure to make system changes: %s", err.what());
      toret = -1;
    }

    lbx_log_debug("installing driver is finished, status was: 0x%x",
                  (unsigned) toret);

    return toret;
  }

  // TODO: de-initialize
  auto ret_ole = OleInitialize(NULL);
  if (ret_ole != S_OK) throw std::runtime_error("couldn't initialize ole!");

  INITCOMMONCONTROLSEX icex;
  safe::zero_object(icex);
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
  safe::global_webdav_init();

  auto SAFE_DUPLICATE_INSTANCE_MSG =
    RegisterWindowMessage(SAFE_DUPLICATE_INSTANCE_MESSAGE_NAME);
  if (!SAFE_DUPLICATE_INSTANCE_MSG) {
    throw std::runtime_error("Couldn't get duplicate instance message");
  }

  exit_if_not_single_app_instance([&] (DWORD proc_id) {
      AllowSetForegroundWindow(proc_id);
      PostMessage(HWND_BROADCAST, SAFE_DUPLICATE_INSTANCE_MSG, 0, 0);
      return 0;
    });

  auto TASKBAR_CREATED_MSG =
    RegisterWindowMessage(TASKBAR_CREATED_MESSAGE_NAME);
  if (!TASKBAR_CREATED_MSG) {
    throw std::runtime_error("Couldn't register for TaskbarCreated message");
  }

  auto native_fs = safe::create_native_fs();

  auto tray_menu_handle = CreatePopupMenu();
  if (!tray_menu_handle) w32util::throw_windows_error();

  auto app_directory = w32util::get_folder_path(CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                                                SHGFP_TYPE_CURRENT);
  auto app_directory_path = native_fs->pathFromString(app_directory).join(PRODUCT_NAME_A);

  // make `app_directory_path` if it doesn't already exists
  try { native_fs->mkdir(app_directory_path); }
  catch (const std::system_error & err) {
    if (err.code() != std::errc::file_exists) throw;
  }

  auto recently_used_paths_storage_path =
    app_directory_path.join(SAFE_RECENTLY_USED_PATHS_DB_FILE_NAME);
  auto path_store =
    safe::RecentlyUsedPathStoreV1(native_fs,
                                  recently_used_paths_storage_path,
                                  SAFE_RECENTLY_USED_PATHS_MENU_NUM_ITEMS);

  auto first_run_cookie_path = app_directory_path.join(SAFE_APP_STARTED_COOKIE_FILENAME);

  auto wd = WindowData {
    /*.native_fs = */std::move(native_fs),
    /*.mounts = */std::vector<safe::win::MountDetails>(),
    /*.is_stopping = */false,
    /*.TASKBAR_CREATED_MSG = */TASKBAR_CREATED_MSG,
    /*.SAFE_DUPLICATE_INSTANCE_MSG = */SAFE_DUPLICATE_INSTANCE_MSG,
    /*.last_foreground_window = */NULL,
    /*.popup_menu_is_open = */false,
    /*.tray_menu = */safe::win::ManagedMenuHandle(tray_menu_handle),
    /*.control_was_pressed_on_tray_open = */false,
    /*.recent_mount_paths_store = */std::move(path_store),
    /*.first_run_cookie_path = */std::move(first_run_cookie_path),
    /*.timer_rev = */0,
  };

  // register our main window class
  WNDCLASSEXW wc;
  safe::zero_object(wc);
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
                            L"Safe Main Window",
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
  while (!wd.mounts.empty()) {
    try {
      unmount_and_stop_drive(nullptr, wd, 0);
    }
    catch (const std::exception & err) {
      lbx_log_error("Failed to unmount: %s", err.what());
    }
  }

  if (ret_getmsg == -1) throw std::runtime_error("getmessage failed!");

  return msg.wParam;
}

WINAPI
int
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow) {
  try {
    return winmain_inner(hInstance, hPrevInstance,
                         lpCmdLine, nCmdShow);
  }
  catch (const std::exception & err) {
    lbx_log_critical("Uncaught exception: %s", err.what());
    auto choice =
      safe::win::report_bug_dialog(nullptr, "An error occured while starting " PRODUCT_NAME_A ".");

    if (choice == safe::win::ReportBugDialogChoice::REPORT_BUG) {
      safe::report_exception(safe::ExceptionLocation::STARTUP,
                             std::current_exception());
    }

    return 0;
  }
}

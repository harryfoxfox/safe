#include <lockbox/lockbox_server.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/fs/FileUtils.h>

#include <encfs/base/optional.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>

#include <cstdint>

#include <windows.h>
#include <CommCtrl.h>
#include <Shlobj.h>

const wchar_t g_szClassName[] = L"myWindowClass";

class WindowData {
public:
  std::shared_ptr<encfs::FsIO> native_fs;
};

enum {
  BUTTON_CLASS = 0x0080,
  EDIT_CLASS = 0x0081,
  STATIC_CLASS = 0x0082,
};

const struct {
  DLGTEMPLATE header;
  struct {
    DLGITEMTEMPLATE header;
    uint16_t class_name;
    const wchar_t *initial_text;
  } items[1];
} encrypted_mount_dialog = {
  .header = {
    .style = 0,
    .dwExtendedStyle = 0,
    .cdit = 0,
    .x = 0,
    .y = 0,
    .cx = 0,
    .cy = 0,
  },
  .items = {
    // mount location label
    {
      .header = {
        .style = 0,
        .dwExtendedStyle = 0,
        .x = 0,
        .y = 0,
        .cx = 0,
        .cy = 0,
        .id = 0,
      },
      .class_name = STATIC_CLASS,
      .initial_text = L"YO",
    },
    // mount location edit text
    // mount location browse button
    // mount loca
  },
};

static
std::wstring
widen(const std::string & s) {
  /* TODO: are these flags good? */
  const DWORD flags = /*MB_COMPOSITE | */MB_ERR_INVALID_CHARS;

  const int required_buffer_size =
    MultiByteToWideChar(CP_UTF8, flags,
                        s.data(), s.size(), NULL, 0);
  if (!required_buffer_size) throw std::runtime_error("error");

  auto out = std::unique_ptr<wchar_t[]>(new wchar_t[required_buffer_size]);

  const int new_return =
    MultiByteToWideChar(CP_UTF8, flags,
                        s.data(), s.size(),
                        out.get(), required_buffer_size);
  if (!new_return) throw std::runtime_error("error");

  return std::wstring(out.get(), required_buffer_size);
}

void
quick_alert(HWND owner, std::string msg, std::string title) {
  MessageBoxW(owner, widen(msg).c_str(), widen(title).c_str(),
              MB_ICONEXCLAMATION | MB_OK);
}

static
std::string
narrow(const std::wstring & s) {
  /* WC_ERR_INVALID_CHARS is only on windows vista and later */
  DWORD flags = 0 /*| WC_ERR_INVALID_CHARS*/;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        s.data(), s.size(),
                        NULL, 0,
                        NULL, NULL);
  if (!required_buffer_size) throw std::runtime_error("error");

  auto out = std::unique_ptr<char[]>(new char[required_buffer_size]);

  const int new_return =
    WideCharToMultiByte(CP_UTF8, flags,
                        s.data(), s.size(),
                        out.get(), required_buffer_size,
                        NULL, NULL);
  if (!new_return) throw std::runtime_error("error");

  return std::string(out.get(), required_buffer_size);
}

WINAPI
opt::optional<std::string>
get_folder_dialog(HWND owner) {
  while (true) {
    wchar_t chosen_name[MAX_PATH];
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select Encrypted Folder";
    bi.ulFlags = BIF_USENEWUI;
    bi.pszDisplayName = chosen_name;
    auto pidllist = SHBrowseForFolderW(&bi);
    if (pidllist) {
      wchar_t file_buffer_ret[MAX_PATH];
      const auto success = SHGetPathFromIDList(pidllist, file_buffer_ret);
      CoTaskMemFree(pidllist);
      if (success) return narrow(file_buffer_ret);
      else {
        std::ostringstream os;
        os << "Your selection \"" << narrow(bi.pszDisplayName) <<
          "\" is not a valid folder!";
        quick_alert(owner, os.str(), "Bad Selection!");
      }
    }
    else return opt::nullopt;
  }
}

WINAPI
void
mount_encrypted_folder_dialog(std::shared_ptr<encfs::FsIO> native_fs,
                              HWND owner) {
  auto maybe_chosen_folder = get_folder_dialog(owner);
  // they pressed cancel
  if (!maybe_chosen_folder) return;
  auto chosen_folder = std::move(*maybe_chosen_folder);

  opt::optional<encfs::Path> maybe_encrypted_directory_path;
  try {
    maybe_encrypted_directory_path =
      native_fs->pathFromString(chosen_folder);
  }
  catch (const std::exception & err) {
    std::ostringstream os;
    os << "Bad path for encrypted directory: " <<
      chosen_folder;
    quick_alert(owner, os.str(), "Bad Path!");
    return;
  }

  auto encrypted_directory_path =
    std::move(*maybe_encrypted_directory_path);

  // TODO: DO THIS ON A DIFFERENT THREAD
  // attempt to read configuration
  opt::optional<encfs::EncfsConfig> maybe_encfs_config;
  try {
    maybe_encfs_config =
      encfs::read_config(native_fs, encrypted_directory_path);
  }
  catch (const encfs::ConfigurationFileDoesNotExist &) {
    // this is fine, we'll just attempt to create it
  }
  catch (const encfs::ConfigurationFileIsCorrupted &) {
    // this is not okay right now, let the user know and exit
    std::ostringstream os;
    os << "The configuration file in folder \"" <<
      chosen_folder << "\" is corrupted";
    quick_alert(owner, os.str(), "Bad Folder");
    return;
  }

  quick_alert(owner, "This folder is cool!", "Cool Folder");
}

// Step 4: the Window Procedure
CALLBACK
LRESULT
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const auto wd = (WindowData *) GetWindowLongPtr(hwnd, GWL_USERDATA);
  switch(msg){
  case WM_CREATE: {
      auto pParent = ((LPCREATESTRUCT)lParam)->lpCreateParams;
      SetWindowLongPtr(hwnd, GWL_USERDATA, (LONG_PTR) pParent);
      break;
  }
  case WM_LBUTTONDOWN: {
    mount_encrypted_folder_dialog(wd->native_fs, hwnd);
    break;
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }
  return 0;
}

WINAPI
int
WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
        LPSTR /*lpCmdLine*/, int nCmdShow)
{
  OleInitialize(NULL);

  auto native_fs = lockbox::create_native_fs();
  WindowData wd = {
    .native_fs = std::move(native_fs),
  };

  // TODO: create a window for the tray icon
  //Step 1: Registering the Window Class
  WNDCLASSEX wc;
  wc.cbSize        = sizeof(WNDCLASSEX);
  wc.style         = 0;
  wc.lpfnWndProc   = WndProc;
  wc.cbClsExtra    = 0;
  wc.cbWndExtra    = 0;
  wc.hInstance     = hInstance;
  wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wc.lpszMenuName  = NULL;
  wc.lpszClassName = g_szClassName;
  wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

  if (!RegisterClassExW(&wc)) {
    MessageBoxW(NULL, L"Window Registration Failed!", L"Error!",
                MB_ICONEXCLAMATION | MB_OK);
    return 0;
  }

  // Step 2: Creating the Window
 auto hwnd = CreateWindowExW(WS_EX_CLIENTEDGE,
                             g_szClassName,
                             L"The title of my window",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
                             NULL, NULL, hInstance, &wd);
  if(hwnd == NULL) {
    MessageBoxW(NULL, L"Window Creation Failed!", L"Error!",
                MB_ICONEXCLAMATION | MB_OK);
    return 0;
  }

  auto button_handle = CreateWindowExW(0,
                                       WC_BUTTON,
                                       L"My Button",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                       250, 250, 30, 20,
                                       hwnd,
                                       (HMENU)1, // or some other unique ID for this ctrl
                                       hInstance,
                                       NULL);
  if (!button_handle) {
    MessageBoxW(NULL, L"Button Creation Failed!", L"Error!",
                MB_ICONEXCLAMATION | MB_OK);
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  // Step 3: The Message Loop
  MSG Msg;
  while (GetMessageW(&Msg, NULL, 0, 0)) {
    TranslateMessage(&Msg);
    DispatchMessage(&Msg);
  }

  return Msg.wParam;
}

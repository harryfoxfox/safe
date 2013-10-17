#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>

#include <cstdint>

#include <windows.h>
#include <CommCtrl.h>
#include <Shlobj.h>

const wchar_t g_szClassName[] = L"myWindowClass";

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

//WINAPI
BOOL mount_encrypted_folder_dialog(HWND owner) {
  BROWSEINFOW bi;
  memset(&bi, 0, sizeof(bi));
  bi.hwndOwner = owner;
  bi.lpszTitle = L"Select Encrypted Folder";
  bi.ulFlags = BIF_USENEWUI;
  auto pidllist = SHBrowseForFolderW(&bi);
  if (pidllist) {
    wchar_t file_buffer_ret[MAX_PATH];
    const bool success = SHGetPathFromIDList(pidllist, file_buffer_ret);
    if (success) {
      std::ostringstream os;
      os << "You Selected: " << narrow(file_buffer_ret);
      MessageBoxW(NULL, widen(os.str()).c_str(), L"Success!",
                  MB_ICONEXCLAMATION | MB_OK);
      std::cout << "the selected file was: " << file_buffer_ret << std::endl;
    }
    CoTaskMemFree(pidllist);
    return true;
  }
  else return false;
}

// Step 4: the Window Procedure
CALLBACK
LRESULT
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch(msg){
  case WM_LBUTTONDOWN: {
    // BEGIN NEW CODE
    mount_encrypted_folder_dialog(hwnd);
  }
    // END NEW CODE
    break;
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
                              NULL, NULL, hInstance, NULL);
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

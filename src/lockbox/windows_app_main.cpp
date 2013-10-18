#include <lockbox/lockbox_server.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/fs/FileUtils.h>

#include <encfs/cipher/MemoryPool.h>

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

enum class ControlClass : uint16_t {
  BUTTON = 0x0080,
  EDIT = 0x0081,
  STATIC = 0x0082,
};

uint16_t
serialize_control_class(ControlClass cls_) {
  return (uint16_t) cls_;
}

void
quick_alert(HWND owner, std::string msg, std::string title);

static
std::wstring
widen(const std::string & s) {
  if (s.empty()) return std::wstring();

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
size_t
narrow_into_buf(const wchar_t *s, size_t num_chars,
                char *out, size_t buf_size_in_bytes) {
  DWORD flags = 0 /*| WC_ERR_INVALID_CHARS*/;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        s, num_chars,
                        out, buf_size_in_bytes,
                        NULL, NULL);
  return required_buffer_size;
}

static
std::string
narrow(const wchar_t *s, size_t num_chars) {
  if (!num_chars) return std::string();

  /* WC_ERR_INVALID_CHARS is only on windows vista and later */
  DWORD flags = 0 /*| WC_ERR_INVALID_CHARS*/;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        s, num_chars,
                        NULL, 0,
                        NULL, NULL);
  if (!required_buffer_size) throw std::runtime_error("error");

  auto out = std::unique_ptr<char[]>(new char[required_buffer_size]);

  const int new_return =
    WideCharToMultiByte(CP_UTF8, flags,
                        s, num_chars,
                        out.get(), required_buffer_size,
                        NULL, NULL);
  if (!new_return) throw std::runtime_error("error");

  return std::string(out.get(), required_buffer_size);
}

static
std::string
narrow(const std::wstring & s) {
  return narrow(s.data(), s.size());
}

void
quick_alert(HWND owner, std::string msg, std::string title) {
  MessageBoxW(owner, widen(msg).c_str(), widen(title).c_str(),
              MB_ICONEXCLAMATION | MB_OK);
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

static
intptr_t
next_align_boundary(intptr_t cur, size_t unit) {
  if (cur % unit) {
    return cur + unit - cur % unit;
  }
  else {
    return cur;
  }
}


typedef uint8_t byte;

static
void
insert_data(std::vector<byte> & v,
            size_t align,
            const byte *a, const byte *b) {
  if ((intptr_t) v.data() % sizeof(DWORD)) {
    throw std::runtime_error("data moved to bad place");
  }
  auto end_ptr = (intptr_t) v.data() + v.size();
  v.insert(v.end(),
           next_align_boundary(end_ptr, align) - end_ptr,
           '0');
  v.insert(v.end(), a, b);                      \
}

template<class T>
void
insert_obj(std::vector<byte> & v, size_t align, const T & obj) {
  insert_data(v, align,
              (byte *) &obj, (byte *) (&obj + 1));
}

static
void
insert_dialog(std::vector<uint8_t> & v,
              DWORD style, const std::string & title,
              WORD cdit,
              short x, short y,
              short cx, short cy) {
  const DLGTEMPLATE dialog_header = {
    .style = style,
    .dwExtendedStyle = 0,
    .cdit = cdit,
    .x = x,
    .y = y,
    .cx = cx,
    .cy = cy,
  };
  insert_obj(v, sizeof(DWORD), dialog_header);

  const uint16_t dialog_menu[] = {0};
  insert_obj(v, sizeof(WORD), dialog_menu);

  const uint16_t dialog_class[] = {0};
  insert_obj(v, sizeof(WORD), dialog_class);

  auto wtitle = widen(title);
  insert_data(v, sizeof(WORD),
              (byte *) wtitle.c_str(),
              (byte *) (wtitle.c_str() + wtitle.size() + 1));
}

static
void
insert_control(std::vector<uint8_t> & v,
               ControlClass cls_, WORD id,
               const std::string & title,
               DWORD style,
               short x, short y,
               short cx, short cy) {

  const DLGITEMTEMPLATE text_item = {
    .style = style,
    .dwExtendedStyle = 0,
    .x = x,
    .y = y,
    .cx = cx,
    .cy = cy,
    .id = id,
  };
  insert_obj(v, sizeof(DWORD), text_item);

  const uint16_t text_item_class[] =
    {0xffff, serialize_control_class(cls_)};
  insert_obj(v, sizeof(WORD), text_item_class);

  auto text_item_title = widen(title);
  insert_data(v, sizeof(WORD),
              (byte *) text_item_title.c_str(),
              (byte *) (text_item_title.c_str() + text_item_title.size() + 1));

  const uint16_t text_item_creation_data[] = {0x0};
  insert_obj(v, sizeof(WORD), text_item_creation_data);
}

const WORD IDC_STATIC = ~0;
const WORD IDPASSWORD = 200;
const auto MAX_PASS_LEN = 256;

CALLBACK
BOOL
get_password_dialog_proc(HWND hwnd, UINT Message,
                         WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG:
    SendDlgItemMessage(hwnd, IDPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    return TRUE;
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK: {
      // securely get what's in dialog box
      auto st1 = encfs::SecureMem((MAX_PASS_LEN + 1) * sizeof(wchar_t));
      auto num_chars =
        GetDlgItemTextW(hwnd, IDPASSWORD,
                        (wchar_t *) st1.data(),
                        st1.size() / sizeof(wchar_t));

      // attempt to clear what's in dialog box
      auto zeroed_bytes =
        std::unique_ptr<wchar_t[]>(new wchar_t[num_chars + 1]);
      memset(zeroed_bytes.get(), 0xaa, num_chars * sizeof(wchar_t));
      zeroed_bytes[num_chars] = 0;
      SetDlgItemTextW(hwnd, IDPASSWORD, zeroed_bytes.get());

      // convert wchars to utf8
      auto st2 = new encfs::SecureMem(MAX_PASS_LEN * 3);
      size_t ret;
      if (num_chars) {
        ret = narrow_into_buf((wchar_t *) st1.data(), num_chars,
                              (char *) st2->data(), st2->size() - 1);
        // should never happen
        if (!ret) throw std::runtime_error("fail");
      }
      else ret = 0;

      st2->data()[ret] = '\0';

      EndDialog(hwnd, (INT_PTR) st2);
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) -1);
      break;
    }
    }
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

WINAPI
opt::optional<encfs::SecureMem>
get_password_dialog(HWND hwnd, const encfs::Path & /*path*/) {
  auto v = std::vector<byte>();

  insert_dialog(v,
                DS_MODALFRAME | WS_POPUP | WS_CAPTION |
                WS_SYSMENU | WS_VISIBLE,
                "Enter Your Password",
                3, 0, 0, 100, 66);

  insert_control(v, ControlClass::STATIC,
                 IDC_STATIC, "Enter Your Password",
                 SS_CENTER | WS_GROUP | WS_VISIBLE | WS_CHILD,
                 15, 10, 70, 33);

  insert_control(v, ControlClass::EDIT,
                 IDPASSWORD, "",
                 ES_PASSWORD | ES_LEFT | WS_BORDER |
                 WS_TABSTOP | WS_VISIBLE | WS_CHILD,
                 10, 20, 80, 12);

  insert_control(v, ControlClass::BUTTON,
                 IDOK, "&OK",
                 BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE | WS_CHILD,
                 25, 40, 50, 14);

  if ((intptr_t) v.data() % sizeof(DWORD)) {
    throw std::runtime_error("bad data");
  }

  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      (LPCDLGTEMPLATE) v.data(),
                      hwnd, get_password_dialog_proc);
  if (ret_ptr == -1) return opt::nullopt;

  auto ret = (encfs::SecureMem *) ret_ptr;
  auto toret = std::move(*ret);
  delete ret;
  return toret;
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

  opt::optional<encfs::SecureMem> maybe_password;
  if (maybe_encfs_config) {
    // ask for password
    // get password from console
    // repeat if user enters invalid password
    while (!maybe_password) {
      maybe_password =
        get_password_dialog(owner, encrypted_directory_path);
      if (!maybe_password) return;

      const auto correct_password =
        encfs::verify_password(*maybe_encfs_config, *maybe_password);

      if (!correct_password) {
        quick_alert(owner, "Incorrect password! Try again",
                    "Incorrect Password");
        maybe_password = opt::nullopt;
      }
    }
  }
  else {
  }

  quick_alert(owner, "This folder is cool!", "Cool Folder");
}

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

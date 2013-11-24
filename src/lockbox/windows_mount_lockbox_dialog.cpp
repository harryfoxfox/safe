CALLBACK
INT_PTR
get_password_dialog_proc(HWND hwnd, UINT Message,
                         WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG:
    w32util::center_window_in_monitor(hwnd);
    w32util::set_default_dialog_font(hwnd);
    SendDlgItemMessage(hwnd, IDPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    return TRUE;
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK: {
      auto secure_pass = securely_read_password_field(hwnd, IDPASSWORD);
      auto st2 = new encfs::SecureMem(std::move(secure_pass));
      EndDialog(hwnd, (INT_PTR) st2);
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) 0);
      break;
    }
    }
    break;
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
static
opt::optional<encfs::SecureMem>
get_password_dialog(HWND hwnd, const encfs::Path & /*path*/) {
  using namespace w32util;

  auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Enter Your Password",
                              0, 0, 100, 66),
                   {
                     CText("Enter Your Password:", IDC_STATIC,
                           15, 10, 70, 33),
                     EditText(IDPASSWORD, 10, 20, 80, 12,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     DefPushButton("&OK", IDOK,
                                   25, 40, 50, 14),
                   }
                   );

  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      hwnd, get_password_dialog_proc);
  if (!ret_ptr) return opt::nullopt;

  auto ret = (encfs::SecureMem *) ret_ptr;
  auto toret = std::move(*ret);
  delete ret;
  return toret;
}

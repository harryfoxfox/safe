#include <windows.h>

CALLBACK
static
INT_PTR
create_new_lockbox_dialog_proc(HWND hwnd, UINT Message,
                               WPARAM wParam, LPARAM /*lParam*/) {
  switch (Message) {
  case WM_INITDIALOG: {
    w32util::center_window_in_monitor(hwnd);
    SendDlgItemMessage(hwnd, IDPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    SendDlgItemMessage(hwnd, IDCONFIRMPASSWORD, EM_LIMITTEXT, MAX_PASS_LEN, 0);
    w32util::set_default_dialog_font(hwnd);
    return TRUE;
  }
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDOK: {
      auto secure_pass_1 =
        securely_read_password_field(hwnd, IDPASSWORD, false);
      auto secure_pass_2 =
        securely_read_password_field(hwnd, IDCONFIRMPASSWORD, false);

      auto num_chars_1 = strlen((char *) secure_pass_1.data());
      auto num_chars_2 = strlen((char *) secure_pass_2.data());
      if (num_chars_1 != num_chars_2 ||
          memcmp(secure_pass_1.data(), secure_pass_2.data(), num_chars_1)) {
        quick_alert(hwnd, "The Passwords do not match!",
                    "Passwords don't match");
        return TRUE;
      }

      if (!num_chars_1) {
        quick_alert(hwnd, "Empty password is not allowed!",
                    "Invalid Password");
        return TRUE;
      }

      clear_field(hwnd, IDPASSWORD, num_chars_1);
      clear_field(hwnd, IDCONFIRMPASSWORD, num_chars_2);

      auto st2 = new encfs::SecureMem(std::move(secure_pass_1));
      EndDialog(hwnd, (INT_PTR) st2);
      break;
    }
    case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) 0);
      break;
    }
    }
    break;
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
static
opt::optional<lockbox::win::MountDetails>
create_new_lockbox_dialog(HWND owner) {
  using namespace w32util;

  // TODO: compute this programmatically
  const float FONT_HEIGHT = 8;
  // 5.5 is the width of the 'm' char  with the default font
  // since it's the widest character we just divide it by 2
  // to get the 'average' width (crude i know)
  const float FONT_WIDTH = 5.5 * 0.7;

  typedef unsigned unit_t;

  const unit_t VERT_MARGIN = 8;
  const unit_t HORIZ_MARGIN = 8;

  const unit_t TOP_MARGIN = VERT_MARGIN;
  const unit_t BOTTOM_MARGIN = VERT_MARGIN;
  const unit_t LEFT_MARGIN = HORIZ_MARGIN;
  const unit_t RIGHT_MARGIN = HORIZ_MARGIN;

  const unit_t MIDDLE_MARGIN = FONT_HEIGHT;

  const unit_t TEXT_WIDTH = 160;
  const unit_t TEXT_HEIGHT = FONT_HEIGHT;

  const unit_t PASS_LABEL_CHARS = strlen("Confirm Password:");

  const unit_t PASS_LABEL_WIDTH = FONT_WIDTH * PASS_LABEL_CHARS;
  const unit_t PASS_LABEL_HEIGHT = FONT_HEIGHT;

  const unit_t DIALOG_WIDTH = PASS_LABEL_WIDTH + 125;

  const unit_t PASS_LABEL_SPACE = 0;
  const unit_t PASS_LABEL_VOFFSET = 2;
  const unit_t PASS_MARGIN = FONT_HEIGHT / 2;

  const unit_t PASS_ENTRY_LEFT_MARGIN =
    LEFT_MARGIN + PASS_LABEL_WIDTH + PASS_LABEL_SPACE;
  const unit_t PASS_ENTRY_WIDTH = DIALOG_WIDTH - RIGHT_MARGIN -
    PASS_ENTRY_LEFT_MARGIN;
  const unit_t PASS_ENTRY_HEIGHT = 12;

  const unit_t BUTTON_WIDTH = 44;
  const unit_t BUTTON_HEIGHT = 14;
  const unit_t BUTTON_SPACING = 4;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              "Create " ENCRYPTED_STORAGE_NAME_A,
                              0, 0, DIALOG_WIDTH,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN +
                              PASS_ENTRY_HEIGHT + PASS_MARGIN +
                              PASS_ENTRY_HEIGHT + MIDDLE_MARGIN +
                              BUTTON_HEIGHT + BOTTOM_MARGIN),
                   {
                     LText(("Create New " ENCRYPTED_STORAGE_NAME_A),
                           IDC_STATIC,
                           LEFT_MARGIN, TOP_MARGIN,
                           TEXT_WIDTH, TEXT_HEIGHT),
                     LText("Location:", IDC_STATIC,
                           0, 0,
                           LOCATION_LABEL_WIDTH, LOCATION_LABEL_HEIGHT),
                     EditText(ID_LOCATION,
                              0, 0,
                              LOCATION_ENTRY_WIDTH, LOCATION_ENTRY_HEIGHT),
                     LText("Name:", IDC_STATIC,
                           0, 0,
                           LOCATION_LABEL_WIDTH, LOCATION_LABEL_HEIGHT),
                     EditText(ID_NAMe,
                              0, 0,
                              LOCATION_ENTRY_WIDTH, LOCATION_ENTRY_HEIGHT),
                     LText("Password:", IDC_STATIC,
                           LEFT_MARGIN,
                           TOP_MARGIN +
                           TEXT_HEIGHT + MIDDLE_MARGIN + PASS_LABEL_VOFFSET,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDPASSWORD,
                              PASS_ENTRY_LEFT_MARGIN,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     LText("Password:", IDC_STATIC,
                           LEFT_MARGIN,
                           TOP_MARGIN +
                           TEXT_HEIGHT + MIDDLE_MARGIN +
                           PASS_ENTRY_HEIGHT + PASS_MARGIN + PASS_LABEL_VOFFSET,
                           PASS_LABEL_WIDTH, PASS_LABEL_HEIGHT),
                     EditText(IDCONFIRMPASSWORD,
                              PASS_ENTRY_LEFT_MARGIN,
                              TOP_MARGIN +
                              TEXT_HEIGHT + MIDDLE_MARGIN +
                              PASS_ENTRY_HEIGHT + PASS_MARGIN,
                              PASS_ENTRY_WIDTH, PASS_ENTRY_HEIGHT,
                              ES_PASSWORD | ES_LEFT |
                              WS_BORDER | WS_TABSTOP),
                     PushButton("&Cancel", IDCANCEL,
                                DIALOG_WIDTH - RIGHT_MARGIN - BUTTON_WIDTH,
                                TOP_MARGIN +
                                TEXT_HEIGHT + MIDDLE_MARGIN +
                                PASS_ENTRY_HEIGHT + PASS_MARGIN +
                                PASS_ENTRY_HEIGHT + MIDDLE_MARGIN,
                                BUTTON_WIDTH, BUTTON_HEIGHT),
                     DefPushButton("&OK", IDOK,
                                   DIALOG_WIDTH -
                                   RIGHT_MARGIN - BUTTON_WIDTH -
                                   BUTTON_SPACING - BUTTON_WIDTH,
                                   TOP_MARGIN +
                                   TEXT_HEIGHT + MIDDLE_MARGIN +
                                   PASS_ENTRY_HEIGHT + PASS_MARGIN +
                                   PASS_ENTRY_HEIGHT + MIDDLE_MARGIN,
                                   BUTTON_WIDTH, BUTTON_HEIGHT),
                   }
                   );

  auto ret_ptr =
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      owner, get_new_password_dialog_proc);
  if (!ret_ptr) return opt::nullopt;

  auto ret = (encfs::SecureMem *) ret_ptr;
  auto toret = std::move(*ret);
  delete ret;
  return toret;
}

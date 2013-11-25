#include <windows.h>

#if 0
  auto maybe_chosen_folder = get_folder_dialog(owner);
  // they pressed cancel
  if (!maybe_chosen_folder) return opt::nullopt;
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
    w32util::quick_alert(owner, os.str(), "Bad Path!");
    return opt::nullopt;
  }

  auto encrypted_directory_path =
    std::move(*maybe_encrypted_directory_path);

  // attempt to read configuration
  opt::optional<encfs::EncfsConfig> maybe_encfs_config;
  try {
    maybe_encfs_config =
      w32util::modal_call(owner, "Reading configuration...",
                          "Reading configuration...",
                          encfs::read_config,
                          native_fs, encrypted_directory_path);
    // it was canceled
    if (!maybe_encfs_config) return opt::nullopt;
  }
  catch (const encfs::ConfigurationFileDoesNotExist &) {
    // this is fine, we'll just attempt to create it
  }
  catch (const encfs::ConfigurationFileIsCorrupted &) {
    // this is not okay right now, let the user know and exit
    std::ostringstream os;
    os << "The configuration file in folder \"" <<
      chosen_folder << "\" is corrupted";
    w32util::quick_alert(owner, os.str(), "Bad Folder");
    return opt::nullopt;
  }

  opt::optional<encfs::SecureMem> maybe_password;
  if (maybe_encfs_config) {
    // ask for password
    while (!maybe_password) {
      maybe_password =
        get_password_dialog(owner, encrypted_directory_path);
      if (!maybe_password) return opt::nullopt;

      lbx_log_debug("verifying password...");
      const auto correct_password =
        w32util::modal_call(owner, "Verifying Password...",
                            "Verifying password...",
                            encfs::verify_password,
                            *maybe_encfs_config, *maybe_password);
      lbx_log_debug("verifying done!");
      if (!correct_password) return opt::nullopt;

      if (!*correct_password) {
        w32util::quick_alert(owner, "Incorrect password! Try again",
                    "Incorrect Password");
        maybe_password = opt::nullopt;
      }
    }
  }
  else {
    // check if the user wants to create an encrypted container
    auto create =
      confirm_new_encrypted_container(owner, encrypted_directory_path);
    if (!create) return opt::nullopt;

    // create new password
    maybe_password =
      get_new_password_dialog(owner, encrypted_directory_path);
    if (!maybe_password) return opt::nullopt;

    const auto use_case_safe_filename_encoding = true;
    maybe_encfs_config =
      w32util::modal_call(owner,
                          "Creating New Configuration...",
                          "Creating new configuration...",
                          encfs::create_paranoid_config,
                          *maybe_password,
                          use_case_safe_filename_encoding);
    if (!maybe_encfs_config) return opt::nullopt;

    auto modal_completed =
      w32util::modal_call_void(owner,
                               "Saving New Configuration...",
                               "Saving new configuration...",
                               encfs::write_config,
                               native_fs, encrypted_directory_path,
                               *maybe_encfs_config);
    if (!modal_completed) return opt::nullopt;
  }

  // okay now we have:
  // * a valid path
  // * a valid config
  // * a valid password
  // we're ready to mount the drive

  auto maybe_mount_details =
    w32util::modal_call(owner,
                        ("Starting New "
                         ENCRYPTED_STORAGE_NAME_A
                         "..."),
                        ("Starting new "
                         ENCRYPTED_STORAGE_NAME_A
                         "..."),
                        lockbox::win::mount_new_encfs_drive,
                        native_fs,
                        encrypted_directory_path,
                        *maybe_encfs_config,
                        *maybe_password);

  if (!maybe_mount_details) return opt::nullopt;

  maybe_mount_details->open_mount();

  bubble_msg(owner,
             "Success",
             "You've successfully started \"" +
             maybe_mount_details->get_mount_name() +
             ".\"");

  return std::move(*maybe_mount_details);
#endif

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

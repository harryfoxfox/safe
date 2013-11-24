enum {
  IDCBLURB = 100,
  IDCGETSOURCECODE,
  IDCCREATELOCKBOX,
};

CALLBACK
INT_PTR
about_dialog_proc(HWND hwnd, UINT Message,
                  WPARAM wParam, LPARAM /*lParam*/) {

  switch (Message) {
  case WM_INITDIALOG: {
    w32util::set_default_dialog_font(hwnd);

    // position everything
    typedef unsigned unit_t;

    // compute size of about string
    auto text_hwnd = GetDlgItem(hwnd, IDCBLURB);
    if (!text_hwnd) throw w32util::windows_error();

    const unit_t BLURB_TEXT_WIDTH = 350;

    const unit_t BUTTON_WIDTH_SRCCODE_DLG = 55;
    const unit_t BUTTON_WIDTH_CREATELB_DLG = 100;
    const unit_t BUTTON_HEIGHT_DLG = 14;

    RECT r;
    r.left = BUTTON_WIDTH_SRCCODE_DLG;
    r.right = BUTTON_WIDTH_CREATELB_DLG;
    r.top = BUTTON_HEIGHT_DLG;
    auto success_map = MapDialogRect(hwnd, &r);
    if (!success_map) throw w32util::windows_error();

    const unit_t BUTTON_WIDTH_SRCCODE = r.left;
    const unit_t BUTTON_WIDTH_CREATELB = r.right;
    const unit_t BUTTON_HEIGHT = r.top;

    const unit_t BUTTON_SPACING = 8;

    auto blurb_text = w32util::widen(LOCKBOX_ABOUT_BLURB);

    auto dc = GetDC(text_hwnd);
    if (!dc) throw w32util::windows_error();
    auto _release_dc_1 = lockbox::create_deferred(ReleaseDC, text_hwnd, dc);

    auto hfont = (HFONT) SendMessage(text_hwnd, WM_GETFONT, 0, 0);
    if (!hfont) return FALSE;
    auto font_dc = SelectObject(dc, hfont);
    if (!font_dc) throw w32util::windows_error();

    auto w = BLURB_TEXT_WIDTH;
    RECT rect;
    lockbox::zero_object(rect);
    rect.right = w;
    auto h = DrawText(dc, blurb_text.data(), blurb_text.size(), &rect,
                      DT_CALCRECT | DT_NOCLIP | DT_LEFT | DT_WORDBREAK);
    if (!h) throw w32util::windows_error();
    w = rect.right;

    auto margin = 8;
    const unit_t DIALOG_WIDTH = margin + w + margin;
    const unit_t DIALOG_HEIGHT =
      margin + h + margin + BUTTON_HEIGHT + margin;

    // set up text window
    auto success_set_wtext = SetWindowText(text_hwnd, blurb_text.c_str());
    if (!success_set_wtext) throw w32util::windows_error();
    auto set_client_area_1 = SetClientSizeInLogical(text_hwnd, true,
                                                    margin, margin,
                                                    w, h);
    if (!set_client_area_1) throw w32util::windows_error();

    // create "create lockbox" button
    auto create_lockbox_hwnd = GetDlgItem(hwnd, IDCCREATELOCKBOX);
    if (!create_lockbox_hwnd) throw w32util::windows_error();

    SetClientSizeInLogical(create_lockbox_hwnd, true,
                           DIALOG_WIDTH -
                           margin - BUTTON_WIDTH_SRCCODE -
                           BUTTON_SPACING - BUTTON_WIDTH_CREATELB,
                           margin + h + margin,
                           BUTTON_WIDTH_CREATELB, BUTTON_HEIGHT);

    // set focus on create lockbox button
    PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM) create_lockbox_hwnd, TRUE);

    // create "get source code" button
    auto get_source_code_hwnd = GetDlgItem(hwnd, IDCGETSOURCECODE);
    if (!get_source_code_hwnd) throw w32util::windows_error();

    SetClientSizeInLogical(get_source_code_hwnd, true,
                           DIALOG_WIDTH -
                           margin - BUTTON_WIDTH_SRCCODE,
                           margin + h + margin,
                           BUTTON_WIDTH_SRCCODE, BUTTON_HEIGHT);

    auto set_client_area_2 = SetClientSizeInLogical(hwnd, true, 0, 0,
                                                    DIALOG_WIDTH,
                                                    DIALOG_HEIGHT);
    if (!set_client_area_2) throw w32util::windows_error();

    w32util::center_window_in_monitor(hwnd);

    return TRUE;
  }

  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case IDCGETSOURCECODE: {
      open_src_code(hwnd);
      return TRUE;
    }
    case IDCCREATELOCKBOX: case IDCANCEL: {
      EndDialog(hwnd, (INT_PTR) LOWORD(wParam));
      return TRUE;
    }
    default: return FALSE;
    }

    // not reached
    assert(false);
  }

  case WM_DESTROY: {
    w32util::cleanup_default_dialog_font(hwnd);
    // we don't actually handle this
    return FALSE;
  }

  default: return FALSE;
  }

  // not reached
  assert(false);
}

WINAPI
static
INT_PTR
about_dialog(HWND hwnd) {
  using namespace w32util;

  const auto dlg =
    DialogTemplate(DialogDesc(DEFAULT_MODAL_DIALOG_STYLE | WS_VISIBLE,
                              ("Welcome to "
                               PRODUCT_NAME_A
                               "!"),
                              0, 0, 500, 500),
                   {
                     LText("", IDCBLURB,
                           0, 0, 0, 0),
                     PushButton("&Get Source Code", IDCGETSOURCECODE,
                                0, 0, 0, 0),
                     DefPushButton(("&Start or Create a "
                                    ENCRYPTED_STORAGE_NAME_A),
                                   IDCCREATELOCKBOX,
                                   0, 0, 0, 0),
                   }
                   );

  return
    DialogBoxIndirect(GetModuleHandle(NULL),
                      dlg.get_data(),
                      hwnd, about_dialog_proc);
}


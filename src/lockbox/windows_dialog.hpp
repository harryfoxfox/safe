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

#ifndef __lockbox_windows_dialog_hpp
#define __lockbox_windows_dialog_hpp

#include <vector>

#include <cstdint>

#include <lockbox/util.hpp>
#include <lockbox/windows_string.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#define __MUTEX_WIN32_CS_DEFINED_LAM
#endif
#include <windows.h>
#ifdef __MUTEX_WIN32_CS_DEFINED_LAM
#undef WIN32_LEAN_AND_MEAN
#undef __MUTEX_WIN32_CS_DEFINED_LAM
#endif

namespace w32util {

const DWORD DEFAULT_MODAL_DIALOG_STYLE = (DS_MODALFRAME | WS_POPUP |
                                          WS_SYSMENU | WS_CAPTION);

namespace _int {
  typedef uint8_t byte;
}

using _int::byte;

enum class ControlClass : uint16_t {
  INVALID = 0x0,
  BUTTON = 0x0080,
  EDIT = 0x0081,
  STATIC = 0x0082,
};

inline
uint16_t
serialize_control_class(ControlClass cls_) {
  return (uint16_t) cls_;
}

inline
intptr_t
next_align_boundary(intptr_t cur, size_t unit) {
  if (cur % unit) {
    return cur + unit - cur % unit;
  }
  else {
    return cur;
  }
}

inline
void
ensure_aligned(const std::vector<byte> & v) {
  // TODO: make the vector use an aligned allocator
  // instead of this runtime error
  if ((intptr_t) v.data() % sizeof(DWORD)) {
    throw std::runtime_error("data moved to bad place");
  }
}

inline
void
insert_data(std::vector<byte> & v,
            size_t align,
            const byte *a, const byte *b) {
  ensure_aligned(v);
  auto end_ptr = (intptr_t) v.data() + v.size();
  v.insert(v.end(),
           next_align_boundary(end_ptr, align) - end_ptr,
           '0');
  v.insert(v.end(), a, b);                      \
}

template<class T>
inline
void
insert_obj(std::vector<byte> & v, size_t align, const T & obj) {
  insert_data(v, align,
              (byte *) &obj, (byte *) (&obj + 1));
}

class DialogTemplate;

class DialogItemDesc {
  DWORD _style;
  std::string _title;
  WORD _id;
  short _x;
  short _y;
  short _cx;
  short _cy;
  bool _is_str_class;
  // could be a union but i don't feel like writing
  // a custom destructor right now
  ControlClass _cls;
  LPCWSTR _str_cls;

  void serialize(std::vector<byte> & v) const {
    const DLGITEMTEMPLATE text_item = {
      .style = _style,
      .dwExtendedStyle = 0,
      .x = _x,
      .y = _y,
      .cx = _cx,
      .cy = _cy,
      .id = _id,
    };
    insert_obj(v, sizeof(DWORD), text_item);

    if (_is_str_class) {
      insert_data(v, sizeof(WORD),
                  (byte *) _str_cls,
                  (byte *) (_str_cls + wcslen(_str_cls) + 1));
    }
    else {
      const uint16_t text_item_class[] =
        {0xffff, serialize_control_class(_cls)};
      insert_obj(v, sizeof(WORD), text_item_class);
    }

    auto text_item_title = widen(_title);
    insert_data(v, sizeof(WORD),
                (byte *) text_item_title.c_str(),
                (byte *) (text_item_title.c_str() +
                          text_item_title.size() + 1));

    const uint16_t text_item_creation_data[] = {0x0};
    insert_obj(v, sizeof(WORD), text_item_creation_data);
  }

private:
  DialogItemDesc(DWORD style, std::string title,
                 WORD id,
                 short x, short y,
                 short cx, short cy, bool is_str_class,
                 ControlClass cls, LPCWSTR str_cls)
    : _style(style)
    , _title(std::move(title))
    , _id(id)
    , _x(x)
    , _y(y)
    , _cx(cx)
    , _cy(cy)
    , _is_str_class(is_str_class)
    , _cls(cls)
    , _str_cls(str_cls) {}

public:
  DialogItemDesc(DWORD style, std::string title,
                 ControlClass cls, WORD id,
                 short x, short y,
                 short cx, short cy)
    : DialogItemDesc(style, std::move(title),
                     id, x, y, cx, cy, false, cls, L"") {}

  DialogItemDesc(DWORD style, std::string title,
                 LPCWSTR cls, WORD id,
                 short x, short y,
                 short cx, short cy)
    : DialogItemDesc(style, std::move(title),
                     id, x, y, cx, cy, true,
                     ControlClass::INVALID, cls) {}

  friend class DialogTemplate;
};

class FontDescription {
  uint16_t _pt_size;
  std::string _name;

  void serialize(std::vector<byte> & v) const {
    insert_obj(v, sizeof(WORD), _pt_size);

    auto font_name = widen(_name);
    insert_data(v, 1,
                (byte *) font_name.c_str(),
                (byte *) (font_name.c_str() +
                          font_name.size() + 1));
  }

public:
  FontDescription(uint16_t pt_size, std::string name)
    : _pt_size(pt_size)
    , _name(std::move(name)) {}

  friend class DialogDesc;
};

class DialogDesc {
  DWORD _style;
  std::string _title;
  short _x;
  short _y;
  short _cx;
  short _cy;
  opt::optional<FontDescription> _font;

  void serialize(std::vector<byte> & v, WORD num_items) const {
    const DLGTEMPLATE dialog_header = {
      .style = _style | (_font ? DS_SETFONT : 0),
      .dwExtendedStyle = 0,
      .cdit = num_items,
      .x = _x,
      .y = _y,
      .cx = _cx,
      .cy = _cy,
    };
    insert_obj(v, sizeof(DWORD), dialog_header);

    const uint16_t dialog_menu[] = {0};
    insert_obj(v, sizeof(WORD), dialog_menu);

    const uint16_t dialog_class[] = {0};
    insert_obj(v, sizeof(WORD), dialog_class);

    auto wtitle = widen(_title);
    insert_data(v, sizeof(WORD),
                (byte *) wtitle.c_str(),
                (byte *) (wtitle.c_str() + wtitle.size() + 1));

    if (_font) _font->serialize(v);
  }

public:
  DialogDesc(DWORD style, std::string title,
             short x, short y,
             unsigned short cx, unsigned short cy,
             opt::optional<FontDescription> font = opt::nullopt)
    : _style(style)
    , _title(std::move(title))
    , _x(x)
    , _y(y)
    , _cx(cx)
    , _cy(cy)
    , _font(std::move(font)) {
  }

  friend class DialogTemplate;
};

// pipe dream: make this constexpr
// (perhaps instead an initializer list of pairs)
class DialogTemplate {
  std::vector<byte> _data;

public:
  DialogTemplate(const DialogDesc & desc,
                 const std::vector<DialogItemDesc> & items) {
    desc.serialize(_data, items.size());
    for (const auto & item : items) {
      item.serialize(_data);
    }
  }

  LPCDLGTEMPLATE get_data() const {
    ensure_aligned(_data);
    return (LPCDLGTEMPLATE) _data.data();
  }
};

// these are defined to match resource file syntax

inline
DialogItemDesc
PushButton(std::string text,
           WORD id, short x, short y,
           short width, short height,
           DWORD style = BS_PUSHBUTTON | WS_TABSTOP) {
  //                          DWORD extended_style = 0) {
  return DialogItemDesc(style | WS_VISIBLE | WS_CHILD,
                        std::move(text),
                        ControlClass::BUTTON,
                        id, x, y, width, height);
}

inline
DialogItemDesc
DefPushButton(std::string text,
              WORD id, short x, short y,
              short width, short height,
              DWORD style = BS_DEFPUSHBUTTON | WS_TABSTOP) {
  return DialogItemDesc(style | WS_VISIBLE | WS_CHILD,
                        std::move(text),
                        ControlClass::BUTTON,
                        id, x, y, width, height);
}

inline
DialogItemDesc
CText(std::string text,
      WORD id, short x, short y,
      short width, short height,
      DWORD style = SS_CENTER | WS_GROUP) {
  return DialogItemDesc(style | WS_VISIBLE | WS_CHILD,
                        std::move(text),
                        ControlClass::STATIC,
                        id,
                        x, y, width, height);
}

inline
DialogItemDesc
LText(std::string text,
      WORD id, short x, short y,
      short width, short height,
      DWORD style = SS_LEFT | WS_GROUP) {
  return DialogItemDesc(style | WS_VISIBLE | WS_CHILD,
                        std::move(text),
                        ControlClass::STATIC,
                        id,
                        x, y, width, height);
}

inline
DialogItemDesc
EditText(WORD id, short x, short y,
         short width, short height,
         DWORD style = ES_LEFT | WS_BORDER | WS_TABSTOP) {
  return DialogItemDesc(style | WS_VISIBLE | WS_CHILD,
                        "", ControlClass::EDIT,
                        id,
                        x, y, width, height);
}

inline
DialogItemDesc
Control(std::string text, WORD id, LPCWSTR cls_,
        DWORD style,
        short x, short y,
        short width, short height) {
  return DialogItemDesc(style | WS_VISIBLE | WS_CHILD,
                        std::move(text), cls_,
                        id,
                        x, y, width, height);
}

template<class T>
inline
T
center_offset(T container_width, T contained_width) {
  return (container_width - contained_width) / 2;
}

}

#endif

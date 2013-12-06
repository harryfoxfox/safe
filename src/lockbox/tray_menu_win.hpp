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

#ifndef __lockbox_windows_menu_hpp
#define __lockbox_windows_menu_hpp

#include <lockbox/util.hpp>

#include <lockbox/tray_menu.hpp>
#include <lockbox/windows_menu.hpp>

namespace lockbox { namespace win {

struct DestroyMenuDestroyer {
  void operator()(HMENU a) {
   auto ret = DestroyMenu(a);
   if (!ret) throw std::runtime_error("couldn't free!");
  }
};

typedef lockbox::ManagedResource<HMENU, DestroyMenuDestroyer> ManagedMenuHandle;

constexpr size_t _MENU_ACTION_BITS = lockbox::numbits<lockbox::tray_menu_action_arg_t>::value;

inline
std::tuple<lockbox::TrayMenuAction, lockbox::tray_menu_action_arg_t>
decode_menu_id(UINT menu_id) {
  return std::make_tuple((lockbox::TrayMenuAction) (menu_id >> _MENU_ACTION_BITS),
                         menu_id & lockbox::create_bit_mask<UINT>(_MENU_ACTION_BITS));
}

inline
UINT
encode_menu_id(lockbox::TrayMenuAction action,
               lockbox::tray_menu_action_arg_t action_arg) {
  static_assert(_MENU_ACTION_BITS == lockbox::numbits<decltype(action_arg)>::value, "invalid argument");
  static_assert(sizeof(UINT) >= sizeof(action_arg), "UINT IS TOO SMALL");
  static_assert(sizeof(UINT) >= sizeof(action), "UINT Is too small");
  auto action_int = static_cast<UINT>(action);
  assert(!action_int ||
         lockbox::numbitsf(action_int) - lockbox::numbitsf(action_arg) >
         lockbox::position_of_highest_bit_set(action_int));
  return (action_int << _MENU_ACTION_BITS) | action_arg;
}

class TrayMenuItem {
public:
  bool
  set_tooltip(std::string tooltip) {
    // we don't support menu tooltips
    (void) tooltip;
    return false;
  }

  bool
  set_property(std::string name, std::string value) {
    // we don't support menu tooltips
    (void) name;
    (void) value;
    return false;
  }
};

class TrayMenu {
private:
  ManagedMenuHandle _mh;

public:
  TrayMenu(ManagedMenuHandle mh) : _mh(std::move(mh)) {}

  TrayMenuItem
  append_item(std::string title,
              lockbox::TrayMenuAction action,
              lockbox::tray_menu_action_arg_t action_arg = 0) {
    w32util::menu_append_string_item(_mh.get(), false, title,
                                     encode_menu_id(action, action_arg));
    return TrayMenuItem();
  }

  void
  append_separator() {
    return w32util::menu_append_separator(_mh.get());
  }
};

}}

#endif

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

#ifndef __lockbox_lockbox_tray_menu_hpp
#define __lockbox_lockbox_tray_menu_hpp

#include <cstdint>

namespace lockbox {

enum class TrayMenuAction : uint16_t {
  UNMOUNT,
  OPEN,
  CREATE,
  MOUNT,
  /*  MOUNT_RECENT,
      CLEAR_RECENTS, */
  ABOUT_APP,
  TEST_BUBBLE,
  TRIGGER_BREAKPOINT,
  QUIT_APP,
};

typedef uint16_t tray_menu_action_arg_t;

template<typename Menu, typename MountList/*, typename RecentMountStore*/>
void
populate_tray_menu(Menu & menu,
                   const MountList & mounts,
                   /*                   const RecentMountStore & recent_mounts,*/
                   bool show_alternative_menu) {
  // Menu is:
  // [ (Open | Unmount) "<mount name>" ]
  // ...
  // [ <separator> ]
  // Create New...
  // Mount Existing...
  // Mount Recent >
  //   [ <folder icon> <mount name> ]
  //   ...
  //   [ <separator> ]
  //   Clear Menu
  // <separator>
  // Get Source Code
  // Quit Bitvault
  // [ <separator> ]
  // [ Test Bubble ]

  const bool show_unmount = show_alternative_menu;
  std::string mount_verb_string;
  TrayMenuAction mount_action_id;
  if (show_unmount) {
    mount_verb_string = "Unmount";
    mount_action_id = TrayMenuAction::UNMOUNT;
  }
  else {
    mount_verb_string = "Open";
    mount_action_id = TrayMenuAction::OPEN;
  }

  tray_menu_action_arg_t mount_tag = 0;
  for (const auto & md : mounts) {
    auto title = mount_verb_string + " \"" + md.get_mount_name() + "\"";
    menu.append_item(title, mount_action_id, mount_tag);
    ++mount_tag;
    assert(mount_tag);
  }

  if (mount_tag) menu.append_separator();

  // Create a New Bitvault
  menu.append_item("Create New...", TrayMenuAction::CREATE);

  // Mount an Existing Bitvault
  menu.append_item("Mount Existing...", TrayMenuAction::MOUNT);

  // create "Mount Recent" submenu
  /*
  {
    auto & sub_menu_handle = menu_handle.append_menu("Mount Recent");

    tray_menu_action_arg_t sub_tag = 0;
    for (const auto & p : recent_mounts.recently_used_paths()) {
      auto & item = sub_menu_handle.append_item(p.basename(),
                                                TRAY_MENY_MOUNT_RECENT, sub_tag);
      item.set_tooltip(p);
      item.set_property("mac_file_type", "pubilc.folder");
      ++sub_tag;
    }

    if (sub_tag) sub_menu_handle.append_separator();

    sub_menu_handle.append_item("Clear Menu", TrayMenuAction::CLEAR_RECENTS);
  }
  */

  menu.append_separator();

  // About Bitvault
  menu.append_item("About Bitvault", TrayMenuAction::ABOUT_APP);

#ifndef NDEBUG
  // Test Bubble
  menu.append_item("Test Bubble", TrayMenuAction::TEST_BUBBLE);

  menu.append_item("Trigger Breakpoint", TrayMenuAction::TRIGGER_BREAKPOINT);
#endif

  menu.append_separator();

  menu.append_item("Quit Bitvault", TrayMenuAction::QUIT_APP);
}

}

#endif

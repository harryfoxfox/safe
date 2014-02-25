/*
  Safe: Encrypted File System
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

#ifndef __windows_safe_dialog_common_hpp
#define __windows_safe_dialog_common_hpp

#include <safe/win/mount.hpp>
#include <w32util/error.hpp>
#include <w32util/string.hpp>

#include <encfs/base/optional.h>

#include <memory>
#include <string>

#include <cassert>

#define ALIGN_LABEL(__NAME, PRECEDING_LABEL)                      \
  const unit_t __NAME ## _LABEL_WIDTH = LABEL_WIDTH; \
  const unit_t __NAME ## _LABEL_HEIGHT = LABEL_HEIGHT; \
  const unit_t __NAME ## _LABEL_LEFT = LEFT_MARGIN; \
  const unit_t __NAME ## _LABEL_TOP = (PRECEDING_LABEL ## _LABEL_TOP + \
                                       PRECEDING_LABEL ## _LABEL_HEIGHT + \
                                       FORM_V_SPACING)

#define ALIGN_TEXT_ENTRY(__NAME) \
  const unit_t __NAME ## _ENTRY_WIDTH = TEXT_ENTRY_WIDTH; \
  const unit_t __NAME ## _ENTRY_HEIGHT = TEXT_ENTRY_HEIGHT; \
  const unit_t __NAME ## _ENTRY_LEFT = (__NAME ## _LABEL_LEFT + \
                                        __NAME ## _LABEL_WIDTH + FORM_H_SPACING); \
  const unit_t __NAME ## _ENTRY_TOP = __NAME ## _LABEL_TOP + LABEL_TO_ENTRY_V_OFFSET

namespace safe { namespace win {

template <typename T>
T
receive_dialog_box_data(INT_PTR ret_ptr) {
  assert(ret_ptr);
  auto md_ptr = std::unique_ptr<T>((T *) ret_ptr);
  return std::move(*md_ptr);
}

template <typename T>
INT_PTR
send_dialog_box_data(T data) {
  return (INT_PTR) new T(std::move(data));
}


inline
opt::optional<safe::win::MountDetails>
receive_mount_details(INT_PTR ret_ptr) {
  if (!ret_ptr) return opt::nullopt;
  auto md_ptr = std::unique_ptr<safe::win::MountDetails>((safe::win::MountDetails *) ret_ptr);
  return std::move(*md_ptr);
}

inline
INT_PTR
send_mount_details(opt::optional<safe::win::MountDetails> maybe_mount_details) {
  return (INT_PTR) new safe::win::MountDetails(std::move(*maybe_mount_details));
}

inline
void
draw_icon_item(LPDRAWITEMSTRUCT pDIS,
               LPCWSTR icon_resource,
               bool is_system) {
  auto width = pDIS->rcItem.right - pDIS->rcItem.left;
  auto height = pDIS->rcItem.bottom - pDIS->rcItem.top;

  auto load_image_width = width;
  auto load_image_height = height;

  if (running_on_winxp()) {
    // NB: windows xp can't load our 256x256 icon
    //     perhaps because it's 32-bit PNG
    //     or because it's not a standard icon size
    //     anyway, this is fine for now (use whatever size LoadImage() can find)
    load_image_width = 0;
    load_image_height = 0;
  }

  HINSTANCE instance = is_system ? nullptr : GetModuleHandle(nullptr);
  auto icon_handle = (HICON) LoadImage(instance,
                                       icon_resource, IMAGE_ICON,
                                       load_image_width, load_image_height,
                                       is_system ? LR_SHARED : 0);
  if (!icon_handle) throw w32util::windows_error();
  auto _release_icon =
    safe::create_deferred(DestroyIcon, icon_handle);
  if (is_system) _release_icon.cancel();

  auto success = DrawIconEx(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top,
                            icon_handle, width, height,
                            0, NULL, DI_NORMAL);
  if (!success) throw w32util::windows_error();
}

inline
decltype(w32util::num_characters(""))
button_width(const std::string & msg) {
  auto width = w32util::num_characters(msg) * 4;
  auto MIN_BUTTON_WIDTH = (decltype(width)) 10 * 4;
  return std::max(width, MIN_BUTTON_WIDTH);
}

}}

#endif

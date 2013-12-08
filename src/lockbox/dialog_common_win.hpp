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

#ifndef __windows_lockbox_dialog_common_hpp
#define __windows_lockbox_dialog_common_hpp

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

namespace lockbox {

inline
opt::optional<lockbox::win::MountDetails>
receive_mount_details(INT_PTR ret_ptr) {
  if (!ret_ptr) return opt::nullopt;
  auto md_ptr = std::unique_ptr<lockbox::win::MountDetails>((lockbox::win::MountDetails *) ret_ptr);
  return std::move(*md_ptr);
}

inline
INT_PTR
send_mount_details(opt::optional<lockbox::win::MountDetails> maybe_mount_details) {
  return (INT_PTR) new lockbox::win::MountDetails(std::move(*maybe_mount_details));
}

}

#endif

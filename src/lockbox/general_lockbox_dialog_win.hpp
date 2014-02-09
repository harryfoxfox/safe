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

#ifndef _lockbox_general_lockbox_dialog_win_hpp
#define _lockbox_general_lockbox_dialog_win_hpp

#include <string>
#include <type_traits>
#include <vector>

#include <lockbox/lean_windows.h>

namespace lockbox { namespace win {

template <class ChoiceType>
struct Choice {
  std::string message;
  ChoiceType value;

  Choice(std::string msg_, ChoiceType value_)
    : message(std::move(msg_))
    , value(std::move(value_)) {}

  template <class U,
            typename
            std::enable_if<std::is_integral<ChoiceType>::value &&
                           std::is_enum<U>::value &&
                           sizeof(ChoiceType) >= sizeof(U)>::type * = nullptr>
  Choice(const Choice<U> & o)
    : message(o.message)
    , value(static_cast<ChoiceType>(o.value)) {}
};

namespace _int {
  typedef unsigned generic_choice_type;
  typedef std::vector<Choice<generic_choice_type>> generic_choices_type;

  generic_choice_type
  generic_general_lockbox_dialog(HWND, std::string, std::string,
                                 generic_choices_type);
}

template <class ChoiceType, class RangeType>
ChoiceType
general_lockbox_dialog(HWND hwnd,
                       std::string title,
                       std::string msg, RangeType && r) {
  auto choices =
    _int::generic_choices_type(std::forward<RangeType>(r).begin(),
			       std::forward<RangeType>(r).end());
  return static_cast<ChoiceType>(_int::generic_general_lockbox_dialog(hwnd, title, msg, std::move(choices)));
}

}}

#endif

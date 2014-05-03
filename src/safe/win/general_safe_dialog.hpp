/*
  Safe: Encrypted File System
  Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef _safe_general_safe_dialog_win_hpp
#define _safe_general_safe_dialog_win_hpp

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <encfs/base/optional.h>

#include <safe/lean_windows.h>

namespace safe { namespace win {

template<typename ChoiceType>
using ButtonAction = std::function<opt::optional<ChoiceType>()>;

enum class GeneralDialogIcon {
  SAFE,
  NONE,
};

template <class To,
          class From,
          typename
          std::enable_if<std::is_integral<To>::value &&
                         std::is_enum<From>::value &&
                         sizeof(To) >= sizeof(From)>::type * = nullptr>
ButtonAction<To>
convert_button_action(ButtonAction<From> action) {
  return [=] () {
    auto ret = action();
    return (ret
            ? opt::make_optional(static_cast<To>(*ret))
            : opt::nullopt);
  };
}

template <class ChoiceType>
struct Choice {
  std::string message;
  ButtonAction<ChoiceType> fn;

  Choice(std::string msg_, ChoiceType value_)
    : message(std::move(msg_)) {
    fn = [=] () {
      return opt::make_optional(value_);
    };
  }

  template <class Fn>
  Choice(std::string msg_, Fn a)
    : message(std::move(msg_))
    , fn(std::move(a)) {}

  template <class U>
  Choice(const Choice<U> & o)
    : message(o.message)
    , fn(convert_button_action<ChoiceType>(o.fn)) {}
};

namespace _int {
  typedef unsigned generic_choice_type;
  typedef std::vector<Choice<generic_choice_type>> generic_choices_type;

  std::pair<generic_choice_type, bool>
  generic_general_safe_dialog_with_suppression(HWND, std::string, std::string,
                                               generic_choices_type,
                                               opt::optional<ButtonAction<generic_choice_type>>,
                                               GeneralDialogIcon, bool show_suppression_checkbox);
}

template <class ChoiceType, class RangeType>
std::pair<ChoiceType, bool>
general_safe_dialog_with_suppression(HWND hwnd,
                                     std::string title,
                                     std::string msg, RangeType r,
                                     opt::optional<ButtonAction<ChoiceType>> close_action = opt::nullopt,
                                     GeneralDialogIcon t = GeneralDialogIcon::SAFE,
                                     bool show_suppression_checkbox = false) {
  auto choices = _int::generic_choices_type(r.begin(), r.end());
  auto new_close_action = close_action
    ? opt::make_optional(convert_button_action<_int::generic_choice_type>(*close_action))
    : opt::nullopt;

  auto ret =
    _int::generic_general_safe_dialog_with_suppression(hwnd, title, msg,
                                                       std::move(choices),
                                                       new_close_action,
                                                       t, show_suppression_checkbox);

  return std::make_pair(static_cast<ChoiceType>(ret.first), ret.second);
}

template <class ChoiceType, class RangeType>
ChoiceType
general_safe_dialog(HWND hwnd,
                    std::string title,
                    std::string msg, RangeType && r,
                    opt::optional<ButtonAction<ChoiceType>> close_action = opt::nullopt,
                    GeneralDialogIcon t = GeneralDialogIcon::SAFE) {
  return general_safe_dialog_with_suppression(hwnd, std::move(title),
                                              std::move(msg), std::forward<RangeType>(r),
                                              std::move(close_action),
                                              t, false).first;
}

}}

#endif

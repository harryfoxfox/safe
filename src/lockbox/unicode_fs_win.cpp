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

#include <lockbox/unicode_fs_win.hpp>

#include <lockbox/windows_string.hpp>
#include <lockbox/lockbox_winnls.h>

#include <memory>

#include <cassert>

#include <windows.h>

// because this file uses NormalizeString(), IsNormalizedString(), and
// CompareStringOrdinal() it makes our binary require windows vista
// or a downloadable update for XP, from MSDN on IsNormalizedString():
// "Windows XP, Windows Server 2003: The required header file and DLL
//  are part of the "Microsoft Internationalized Domain Name (IDN)
//  Mitigation APIs" download, available at the MSDN Download Center."

namespace lockbox { namespace unicode_fs { namespace win {

std::string
normalize_path_component_for_fs(const std::string & comp) {
  // throw exception if comp is not utf-8
  // normalize to NFC
  auto wstr = w32util::widen(comp);
  auto ret_normalize =
    NormalizeString(NormalizationC, wstr.data(), wstr.size(),
                    NULL, 0);
  if (ret_normalize <= 0) throw w32util::windows_error();

  auto out = std::unique_ptr<wchar_t[]>(new wchar_t[ret_normalize]);
  auto ret_normalize2 =
    NormalizeString(NormalizationC, wstr.data(), wstr.size(),
                    out.get(), ret_normalize);
  if (ret_normalize2 <= 0) throw w32util::windows_error();

  return w32util::narrow(std::wstring(out.get(), ret_normalize2));
}

std::string
normalize_path_component_for_user(const std::string & comp) {
  assert(is_normalized_path_component(comp));
  // this is a no-op
  return comp;
}

bool
is_normalized_path_component(const std::string & comp) {
  auto wstr = w32util::widen(comp);
  SetLastError(ERROR_SUCCESS);
  auto ret = IsNormalizedString(NormalizationC, wstr.data(), wstr.size());
  if (!ret && GetLastError() != ERROR_SUCCESS) {
    throw w32util::windows_error();
  }

  return ret;
}

bool
normalized_path_components_equal(const std::string & comp_a,
                                 const std::string & comp_b) {
  assert(is_normalized_path_component(comp_a));
  assert(is_normalized_path_component(comp_b));
  auto wstr_a = w32util::widen(comp_a);
  auto wstr_b = w32util::widen(comp_b);

  BOOL do_case_insensitive_compare = TRUE;
  auto ret = CompareStringOrdinal(wstr_a.data(), wstr_a.size(),
                                  wstr_b.data(), wstr_b.size(),
                                  do_case_insensitive_compare);
  if (!ret) throw w32util::windows_error();

  return ret == CSTR_EQUAL;
}

}}}

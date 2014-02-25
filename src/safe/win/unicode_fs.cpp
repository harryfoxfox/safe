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

#include <safe/win/unicode_fs.hpp>

#include <w32util/string.hpp>
#include <safe/safe_winnls.h>
#include <safe/deferred.hpp>

#include <memory>

#include <cassert>

#include <windows.h>

// because this file uses NormalizeString(), IsNormalizedString(), and
// CompareStringOrdinal() it makes our binary require windows vista
// or a downloadable update for XP, from MSDN on IsNormalizedString():
// "Windows XP, Windows Server 2003: The required header file and DLL
//  are part of the "Microsoft Internationalized Domain Name (IDN)
//  Mitigation APIs" download, available at the MSDN Download Center."

namespace safe { namespace unicode_fs { namespace win {

std::string
normalize_path_component_for_fs(const std::string & comp) {
  // throw exception if comp is not utf-8
  // normalize to NFC
  auto wstr = w32util::widen(comp);
  auto ret_normalize =
    NormalizeString(NormalizationC, wstr.data(), wstr.size(),
                    NULL, 0);
  if (ret_normalize <= 0) w32util::throw_windows_error();

  auto out = std::unique_ptr<wchar_t[]>(new wchar_t[ret_normalize]);
  auto ret_normalize2 =
    NormalizeString(NormalizationC, wstr.data(), wstr.size(),
                    out.get(), ret_normalize);
  if (ret_normalize2 <= 0) w32util::throw_windows_error();

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
    w32util::throw_windows_error();
  }

  return ret;
}

typedef struct {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} MyUNICODE_STRING, *MyPUNICODE_STRING;

typedef HRESULT (WINAPI *RtlEqualUnicodeStringType)(const MyPUNICODE_STRING,
                                                    const MyPUNICODE_STRING,
                                                    BOOLEAN);

static
RtlEqualUnicodeStringType
get_rtl_equal_unicode_string() {
  HMODULE mod;
  w32util::check_bool(GetModuleHandleExW,
                      0, L"ntdll.dll", &mod);
  auto _free_mod = safe::create_deferred(FreeLibrary, mod);
  return (RtlEqualUnicodeStringType)
    GetProcAddress(mod, "RtlEqualUnicodeString");
}

typedef int (*CompareStringOrdinalType)(LPCWSTR lpString1,
                                        int cchCount1,
                                        LPCWSTR lpString2,
                                        int cchCount2,
                                        BOOL bIgnoreCase);

static
CompareStringOrdinalType
get_compare_string_ordinal() {
  HMODULE mod;
  w32util::check_bool(GetModuleHandleExW,
                      0, L"kernel32.dll", &mod);
  auto _free_mod = safe::create_deferred(FreeLibrary, mod);
  return (CompareStringOrdinalType)
    GetProcAddress(mod, "CompareStringOrdinal");
}


bool
normalized_path_components_equal(const std::string & comp_a,
                                 const std::string & comp_b) {
  assert(is_normalized_path_component(comp_a));
  assert(is_normalized_path_component(comp_b));
  auto wstr_a = w32util::widen(comp_a);
  auto wstr_b = w32util::widen(comp_b);

  BOOL do_case_insensitive_compare = TRUE;

  // yo if you're reading this
  // you're a hacker
  // holler

  auto MyCompareStringOrdinal = get_compare_string_ordinal();
  if (MyCompareStringOrdinal) {
    auto ret = MyCompareStringOrdinal(wstr_a.data(), wstr_a.size(),
                                      wstr_b.data(), wstr_b.size(),
                                      do_case_insensitive_compare);
    if (!ret) w32util::throw_windows_error();
    return ret == CSTR_EQUAL;
  }

  auto MyRtlEqualUnicodeString = get_rtl_equal_unicode_string();
  if (MyRtlEqualUnicodeString) {
    MyUNICODE_STRING string1 = {
      (USHORT) (wstr_a.size() * sizeof(decltype(wstr_a)::size_type)),
      (USHORT) (wstr_a.size() * sizeof(decltype(wstr_a)::size_type)),
      const_cast<PWSTR>(wstr_a.data()),
    };

    MyUNICODE_STRING string2 = {
      (USHORT) (wstr_b.size() * sizeof(decltype(wstr_b)::size_type)),
      (USHORT) (wstr_b.size() * sizeof(decltype(wstr_b)::size_type)),
      const_cast<PWSTR>(wstr_b.data()),
    };

    return MyRtlEqualUnicodeString(&string1, &string2,
                                   do_case_insensitive_compare);
  }

  throw std::runtime_error("no compare function!");
}

}}}

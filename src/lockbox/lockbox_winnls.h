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

#ifndef _LOCKBOX_WINNLS_H
#define _LOCKBOX_WINNLS_H

#include <lockbox_nlscheck.h>

#include <lockbox/lean_windows.h>

#ifdef __cplusplus
extern "C" {
#endif

WINAPI
int CompareStringOrdinal(
  LPCWSTR lpString1,
  int cchCount1,
  LPCWSTR lpString2,
  int cchCount2,
  BOOL bIgnoreCase
);

#ifndef LOCKBOX_HAVE_WINNLS

typedef enum _NORM_FORM {
  NormalizationOther  = 0,
  NormalizationC      = 0x1,
  NormalizationD      = 0x2,
  NormalizationKC     = 0x5,
  NormalizationKD     = 0x6
} NORM_FORM;

WINAPI
BOOL
IsNormalizedString(
  NORM_FORM NormForm,
  LPCWSTR lpString,
  int cwLength
);

WINAPI
int
NormalizeString(
  NORM_FORM NormForm,
  LPCWSTR lpSrcString,
  int cwSrcLength,
  LPWSTR lpDstString,
  int cwDstLength
);

#endif

#ifdef __cplusplus
}
#endif


#endif

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

#ifndef _safe_resources_win_h
#define _safe_resources_win_h

#include <safe/lean_windows.h>

/*
  Naming conventions lifted from:
  http://en.wikibooks.org/wiki/Windows_Programming/Resource_Script_Reference#Identifiers
*/

#ifdef RC_INVOKED
#define DEFRSRC(a) a
#else
#define DEFRSRC(a) MAKEINTRESOURCEW(a)
#endif

#define IDI_SFX_APP DEFRSRC(101)
#define ID_SFX_RD_INF DEFRSRC(102)
#define ID_SFX_RD_SYS32 DEFRSRC(103)
#define ID_SFX_RD_SYS64 DEFRSRC(104)
#define ID_SFX_UPDATE_DRV DEFRSRC(105)
#define ID_SFX_RD_CAT DEFRSRC(106)

/*
  256 because 1-255 are reserved:
  http://msdn.microsoft.com/en-us/library/windows/desktop/aa381054%28v=vs.85%29.aspx
 */
#define SFX_BIN_RSRC DEFRSRC(256)

#endif

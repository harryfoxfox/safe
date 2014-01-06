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

#ifndef _lockbox_resources_win_h
#define _lockbox_resources_win_h

#include <lockbox/lean_windows.h>

#define _IDI_LBX_APP 101

#ifdef RC_INVOKED
#define IDI_LBX_APP _IDI_LBX_APP
#else
#define IDI_LBX_APP MAKEINTRESOURCEW(_IDI_LBX_APP)
#endif

#endif

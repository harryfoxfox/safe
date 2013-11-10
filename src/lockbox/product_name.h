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

#ifndef _PRODUCT_NAME_H
#define _PRODUCT_NAME_H

#define PRODUCT_NAME_W L"Bitvault"
#define PRODUCT_NAME_A "Bitvault"

#ifdef _UNICODE
#define PRODUCT_NAME PRODUCT_NAME_W
#else
#define PRODUCT_NAME PRODUCT_NAME_A
#endif

#define ENCRYPTED_STORAGE_NAME_W PRODUCT_NAME_W
#define ENCRYPTED_STORAGE_NAME_A PRODUCT_NAME_A
#define ENCRYPTED_STORAGE_NAME PRODUCT_NAME

#endif

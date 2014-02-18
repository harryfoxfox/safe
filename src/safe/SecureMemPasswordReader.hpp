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

#ifndef _SecureMemPasswordReader_incl_
#define _SecureMemPasswordReader_incl_

#include <encfs/fs/PasswordReader.h>

namespace safe {

class SecureMemPasswordReader : public encfs::PasswordReader
{
  encfs::SecureMem _secure_mem;

public:
  SecureMemPasswordReader(encfs::SecureMem a);
  virtual encfs::SecureMem *readPassword(size_t maxLen, bool newPass) override;
};

}

#endif

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

#include <safe/SecureMemPasswordReader.hpp>

#include <stdexcept>

namespace safe {

SecureMemPasswordReader::SecureMemPasswordReader(encfs::SecureMem a)
  : _secure_mem(a) {
  if (!_secure_mem.data() ||
      _secure_mem.data()[_secure_mem.size() - 1] != '\0') {
    throw std::runtime_error("Bad secure mem! must be a c string");
  }
}

encfs::SecureMem *SecureMemPasswordReader::readPassword(size_t /*maxLen*/, bool /*newPass*/) {
  return new encfs::SecureMem(_secure_mem);
}

}

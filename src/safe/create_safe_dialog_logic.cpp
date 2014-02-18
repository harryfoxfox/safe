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

#include <safe/create_safe_dialog_logic.hpp>

#include <encfs/fs/FsIO.h>
#include <encfs/cipher/MemoryPool.h>
#include <encfs/base/optional.h>

#include <memory>
#include <string>

namespace safe {

opt::optional<decltype(make_error_message("", ""))>
verify_create_safe_dialog_fields(const std::shared_ptr<encfs::FsIO> & fs,
                                    const std::string & location,
                                    const std::string & name,
                                    const encfs::SecureMem & password,
                                    const encfs::SecureMem & password_confirm) {
  const static auto _make_error_message = safe::make_error_message;

  // check if location is a well-formed path
  opt::optional<encfs::Path> maybe_location_path;
  try {
    maybe_location_path = fs->pathFromString(location);
  }
  catch (...) {
    return _make_error_message("Bad Location",
                               "The location you have chosen is invalid.");
  }

  auto location_path = std::move(*maybe_location_path);

  // check if location exists
  if (!encfs::file_exists(fs, location_path)) {
    return _make_error_message("Location does not exist",
                               std::string("The location you have chosen, \"") +
                               (const std::string &) location_path +
                               std::string("\" does not exist."));
  }

  // check validity of name field

  // non-zero length
  if (name.empty()) {
    return _make_error_message("Name is Empty",
                               "The name you have entered is empty. Please enter a non-empty name.");
  }

  // we can join it to the location path
  opt::optional<encfs::Path> maybe_container_path;
  try {
    maybe_container_path = location_path.join(name);
  }
  catch (...) {
    return _make_error_message("Bad Name",
                               "The name you have entered is not "
                               "valid for a folder name. "
                               "Please choose a name that's more "
                               "appropriate for a folder, try not "
                               "using special characters.");
  }

  // check if full bitvault path already exists
  if (encfs::file_exists(fs, *maybe_container_path)) {
    return _make_error_message("File Already Exists",
                               "A file already exists "
                               "with the name you have "
                               "chosen, please choose another name.");

  }

  // check validity of password
  auto num_chars_1 = strlen((char *) password.data());

  // check if password is empty
  if (!num_chars_1) {
    return _make_error_message("Invalid Password",
                               "Empty password is not allowed!");
  }

  // check if passwords match
  auto num_chars_2 = strlen((char *) password_confirm.data());
  if (num_chars_1 != num_chars_2 ||
      memcmp(password.data(), password_confirm.data(), num_chars_1)) {
    return _make_error_message("Passwords don't match",
                               "The Passwords do not match!");
  }

  return opt::nullopt;
}

}

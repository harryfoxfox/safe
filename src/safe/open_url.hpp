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

#include <safe/optional.hpp>

#include <string>
#include <utility>
#include <vector>

namespace safe {

class URLQueryArg {
  std::pair<std::string, opt::optional<std::string>> _arg;

public:
  URLQueryArg(std::string name, std::string value)
    : _arg(std::move(name), opt::make_optional(std::move(value))) {}

  URLQueryArg(std::string name)
    : _arg(std::move(name), opt::nullopt) {}

  std::string
  name() const {
    return _arg.first;
  }

  opt::optional<std::string>
  value() const {
    return _arg.second;
  }
};

typedef std::vector<URLQueryArg> URLQueryArgs;

void
open_url(const std::string & escaped_scheme_authority_path,
         const URLQueryArgs & unescaped_args = URLQueryArgs());

}

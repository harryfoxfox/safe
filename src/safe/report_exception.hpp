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

#ifndef __safe_report_exception_hpp
#define __safe_report_exception_hpp

#include <safe/optional.hpp>

#include <exception>
#include <vector>

namespace safe {

// NB: only add to this list, *never* remove
enum class ExceptionLocation {
    SYSTEM_CHANGES,
    STARTUP,
    MOUNT,
    CREATE,
    TRAY_DISPATCH,
    UNEXPECTED,
};

void
report_exception(ExceptionLocation, std::exception_ptr eptr,
                 opt::optional<std::vector<ptrdiff_t>> maybe_offset_stack_trace = opt::nullopt);

}

#endif

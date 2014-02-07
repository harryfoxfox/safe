/*
  Safe Ramdisk:
  A simple Windows driver to emulate a FAT32 formatted disk drive
  backed by paged memory in the Windows kernel.

  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef __safe_nt_helpers_hpp
#define __safe_nt_helpers_hpp

namespace safe_nt {

const char *
ioctl_to_string(ULONG ioctl) noexcept;

const char *
pnp_minor_function_to_string(UCHAR minor) noexcept;

const char *
nt_status_to_string(NTSTATUS status) noexcept;

NTSTATUS
standard_complete_irp_info(PIRP irp, NTSTATUS status, ULONG_PTR info) noexcept;

NTSTATUS
standard_complete_irp(PIRP irp, NTSTATUS status) noexcept;

NTSTATUS
check_parameter_size(PIO_STACK_LOCATION io_stack, size_t s) noexcept;

}

// define a macro for now,
// other options include using a variadic template
// or figuration out stdarg in windows kernel space
// these are better options for type-safety reasons
#ifndef NT_LOG_PREFIX
#define NT_LOG_PREFIX ""
#endif

#define nt_log(...) (STATUS_SUCCESS == \
		     DbgPrint((char *) NT_LOG_PREFIX __VA_ARGS__))
#define nt_log_debug nt_log
#define nt_log_info nt_log
#define nt_log_error nt_log

#endif

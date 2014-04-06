/*
  Safe: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

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

#include <safe/win/helper_binary.hpp>

#include <safe/report_exception.hpp>
#include <safe/exception_backtrace.hpp>
#include <safe/util.hpp>

#include <safe/win/exception_backtrace.hpp>

#include <w32util/string.hpp>
#include <w32util/file.hpp>

#include <exception>

#include <cstdint>

#include <windows.h>
#include <shellapi.h>

const auto ERROR_EXCEPTION = (DWORD) -1;
const auto ERROR_GET_COMMAND_LINE = (DWORD) -2;
const auto ERROR_NOT_ENOUGH_ARGUMENTS = (DWORD) -3;

namespace safe { namespace win {

DWORD
run_helper_binary(bool as_admin,
                  std::string path,
                  std::vector<std::string> arguments) {
  // temp file path that sub-process will write output too
  auto temp_file_path =
    w32util::get_temp_file_name(w32util::get_temp_path(),
                                "SAFETEMP");

  arguments.push_back(temp_file_path);

  std::ostringstream os;
  for (const auto & arg : arguments) {
    os << safe::wrap_quotes(arg) << " ";
  }

  auto application_path_w = w32util::widen(path);
  auto parameters_w = w32util::widen(os.str());

  SHELLEXECUTEINFOW shex;
  safe::zero_object(shex);
  shex.cbSize = sizeof(shex);
  shex.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
  shex.lpVerb = as_admin ? L"runas" : L"open";
  shex.lpFile = application_path_w.c_str();
  shex.lpParameters = parameters_w.c_str();
  shex.nShow = SW_HIDE;

  w32util::check_bool(ShellExecuteExW, &shex);

  if (!shex.hProcess) std::runtime_error("No process?");

  auto _close_process_handle =
    safe::create_deferred(CloseHandle, shex.hProcess);

  w32util::check_call(WAIT_FAILED, WaitForSingleObject,
                      shex.hProcess, INFINITE);

  DWORD exit_code;
  w32util::check_bool(GetExitCodeProcess, shex.hProcess, &exit_code);

  if (exit_code == ERROR_NOT_ENOUGH_ARGUMENTS) {
    throw std::runtime_error("not enough arguments given to binary, this should never happen");
  }
  else if (exit_code == ERROR_GET_COMMAND_LINE) {
    throw std::runtime_error("binary couldn't parse command line");
  }
  else if (exit_code == ERROR_EXCEPTION) {
    // command failed, read about exception info
    auto hfile = w32util::check_invalid_handle(CreateFileW,
                                               w32util::widen(temp_file_path).c_str(),
                                               GENERIC_READ,
                                               FILE_SHARE_READ | FILE_SHARE_DELETE,
                                               nullptr,
                                               OPEN_EXISTING,
                                               FILE_ATTRIBUTE_NORMAL,
                                               nullptr);
    auto _close_file = safe::create_deferred(CloseHandle, hfile);

    w32util::HandleInputStreamBuf<4096> streambuf(hfile);
    std::istream is(&streambuf);
    is.exceptions(std::istream::badbit | std::istream::failbit);

    opt::optional<safe::OffsetBacktrace> maybe_backtrace;
    {
      safe::OffsetBacktrace bt;

      // first read out backtrace
      while (true) {
        // skip spaces (but not \n)
        while (is.peek() == ' ') is.ignore();

        if (is.peek() == '\n') {
          is.ignore();
          break;
        }

        std::ptrdiff_t return_address;
        is >> return_address;
        bt.push_back(return_address);
      }

      if (!bt.empty()) maybe_backtrace = std::move(bt);
    }

    auto read_empty_str_as_none =
      [] (std::istream & is) {
      std::string module;
      getline(is, module);
      return module.empty()
      ? opt::nullopt
      : opt::make_optional(module);
    };

    // read out module
    auto maybe_module = read_empty_str_as_none(is);
    if (!maybe_module) {
      // default module
      maybe_module = w32util::basename(path);
    }

    // read out type
    auto maybe_type_name = read_empty_str_as_none(is);

    std::string arch;
    getline(is, arch);

    std::string value_sizes;
    getline(is, value_sizes);

    // read out what
    std::stringbuf what_buf;
    is >> &what_buf;
    auto what = what_buf.str();
    opt::optional<std::string> maybe_what;
    if (!what.empty()) maybe_what = what;

    throw safe::ExtraBinaryException {
        safe::ExceptionInfo {
          std::move(maybe_type_name), std::move(maybe_what),
          std::move(maybe_module), std::move(maybe_backtrace),
          std::move(arch), std::move(value_sizes),
        }
      };
  }
  else {
    // if this wasn't and error
    return exit_code;
  }
}

DWORD
helper_binary_main_no_argparse(std::string output_path,
                               std::function<DWORD()> fn) {
  DWORD toret = ERROR_EXCEPTION;
  opt::optional<ExceptionInfo> maybe_einfo;
  try {
    toret = fn();
    assert(toret != ERROR_EXCEPTION);
  }
  catch (...) {
    maybe_einfo = safe::extract_exception_info(std::current_exception());
  }

  // we had an error, we need to print details to temp file
  // so parent can report error
  if (maybe_einfo) {
    auto hfile = w32util::check_invalid_handle(CreateFileW,
                                               w32util::widen(output_path).c_str(),
                                               GENERIC_WRITE,
                                               0,
                                               nullptr,
                                               CREATE_ALWAYS,
                                               FILE_ATTRIBUTE_NORMAL,
                                               nullptr);
    auto _close_file = safe::create_deferred(CloseHandle, hfile);

    w32util::HandleOutputStreamBuf<4096> streambuf(hfile);
    std::ostream os(&streambuf);
    os.exceptions(std::ostream::badbit | std::ostream::failbit);

    // output stack trace
    if (maybe_einfo->maybe_offset_backtrace) {
      for (const auto & a : *maybe_einfo->maybe_offset_backtrace) {
        os << a << " ";
      }
    }
    os.put('\n');

    // output module name
    if (maybe_einfo->maybe_module) {
      os << *maybe_einfo->maybe_module;
    }
    os.put('\n');

    // output exception name
    if (maybe_einfo->maybe_type_name) {
      os << *maybe_einfo->maybe_type_name;
    }
    os.put('\n');

    // output arch
    os << maybe_einfo->arch << '\n';

    // output value_sizes
    os << maybe_einfo->value_sizes << '\n';

    // output exception what
    if (maybe_einfo->maybe_what) {
      os << *maybe_einfo->maybe_what;
    }
}

  return toret;
}

DWORD
helper_binary_main(std::function<DWORD(std::vector<std::string>)> fn) {
  int argc;
  auto wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wargv) return ERROR_GET_COMMAND_LINE;

  if (argc < 2) return ERROR_NOT_ENOUGH_ARGUMENTS;

  std::vector<std::string> arguments;
  for (int i = 1; i < argc - 1; ++i) {
    arguments.push_back(w32util::narrow(wargv[i]));
  }

  std::string output_path = w32util::narrow(wargv[argc - 1]);

  return helper_binary_main_no_argparse
    (output_path, [&] { 
      return fn(arguments);
    });
}

}}

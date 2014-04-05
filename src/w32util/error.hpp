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

#ifndef __safe_windows_error_hpp
#define __safe_windows_error_hpp

#include <safe/logging.h>
#include <safe/lean_windows.h>

#include <encfs/base/optional.h>

#include <sstream>
#include <stdexcept>
#include <system_error>

namespace w32util {

inline
std::string
error_message(DWORD err_code) {
  enum {
    MAX_MSG=128,
  };
  wchar_t error_buf_wide[MAX_MSG];
  char error_buf[MAX_MSG];

  DWORD num_chars =
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, 0, err_code, 0,
                   error_buf_wide,
                   sizeof(error_buf_wide) / sizeof(error_buf_wide[0]),
                   NULL);
  if (!num_chars) {
    std::ostringstream os;
    os << std::hex << std::showbase
       << "Couldn't get error message for " << err_code << ", "
       << "FormatMessageW() failed (" << GetLastError() << ")";
    return os.str();
  }

  // clear \r\n
  if (num_chars >= 2) {
    error_buf_wide[num_chars - 2] = L'\0';
    num_chars -= 2;
  }

  const DWORD flags = 0;
  const int required_buffer_size =
    WideCharToMultiByte(CP_UTF8, flags,
                        error_buf_wide, num_chars,
                        error_buf, sizeof(error_buf),
                        NULL, NULL);
  if (!required_buffer_size) {
    std::ostringstream os;
    os << std::hex << std::showbase
       << "Couldn't get error_message for " << err_code << ", "
       << "WideCharToMultibyte() failed (" << GetLastError() << ")";
    return os.str();
  }

  return std::string(error_buf, required_buffer_size);
}

inline
std::string
last_error_message(opt::optional<DWORD> err_code = opt::nullopt) {
  if (!err_code) err_code = GetLastError();
  return error_message(*err_code);
}

class windows_error_category_cls : public std::error_category {
public:
  windows_error_category_cls() {}

  std::error_condition
  default_error_condition(int __i) const noexcept
  {
    // TODO: implement bool equivalent(int code, const std::error_condition &)
    //       if we are equal to other conditions
    switch ((DWORD) __i) {
    case ERROR_FILE_NOT_FOUND: return std::errc::no_such_file_or_directory;
    }
    return std::error_category::default_error_condition(__i);
  }

  virtual const char *name() const noexcept {
    return "windows_error";
  }

  virtual std::string message(int cond) const {
    return error_message((DWORD) cond);
  }
};

inline
const std::error_category &
windows_error_category() noexcept {
  static const windows_error_category_cls windows_error_category_instance;
  return windows_error_category_instance;
}

class windows_error : public std::system_error {
public:
  windows_error(opt::optional<DWORD> err_code = opt::nullopt)
    : std::system_error(err_code ? (int) *err_code : GetLastError(),
			windows_error_category()) {
    static_assert(sizeof(DWORD) <= sizeof(int),
		  "can't store dword in an int!");
  }
};

[[noreturn]]
inline
void
throw_windows_error() {
  // NB: for some reason on g++ the last error is reset 
  //     when called on the same line as "throw"
  auto err_code = GetLastError();
  throw windows_error(err_code);
}

template <class IsBadF, class F, class... Args>
typename std::result_of<F(Args...)>::type
check_call_fn(IsBadF is_bad, F f, Args && ...args) {
  auto ret = f(std::forward<Args>(args)...);
  if (is_bad(ret)) w32util::throw_windows_error();
  return ret;
}

template <class ReturnType, class F, class... Args>
typename std::result_of<F(Args...)>::type
check_call(ReturnType bad, F && f, Args && ...args) {
  auto is_equal_to = [&] (typename std::result_of<F(Args...)>::type in) { return in == bad; };
  return check_call_fn(is_equal_to, std::forward<F>(f),
                       std::forward<Args>(args)...);
}

template <class F, class... Args,
          class ReturnType = typename std::result_of<F(Args...)>::type>
ReturnType
check_good_call(ReturnType good, F && f, Args && ...args) {
  auto is_not_equal_to = [&] (ReturnType in) { return in != good; };
  return check_call_fn(is_not_equal_to, std::forward<F>(f),
                       std::forward<Args>(args)...);
}

template <class F, class... Args,
          class ReturnType = typename std::result_of<F(Args...)>::type>
void
check_error_call(F && f, Args && ...args) {
  auto ret = f(std::forward<Args>(args)...);
  if (ret != ERROR_SUCCESS) throw w32util::windows_error(ret);
}

template <class F, class... Args>
HANDLE
check_invalid_handle(F && f, Args && ...args) {
  return check_call(INVALID_HANDLE_VALUE,
                    std::forward<F>(f),
                    std::forward<Args>(args)...);
}

template <class F, class... Args>
void
check_bool(F && f, Args && ...args) {
  check_call(FALSE,
             std::forward<F>(f),
             std::forward<Args>(args)...);
}

template <class F, class... Args>
typename std::result_of<F(Args...)>::type
check_null(F && f, Args && ...args) {
  return check_call(nullptr,
                    std::forward<F>(f),
                    std::forward<Args>(args)...);
}

class com_error_category_cls : public std::error_category {
public:
  com_error_category_cls() {}

  virtual const char *name() const noexcept {
    return "com_error";
  }

  std::error_condition
  default_error_condition(int cond) const noexcept {
    HRESULT hr = {cond};

    if (FACILITY_WINDOWS == HRESULT_FACILITY(hr)) {
      return windows_error_category().default_error_condition(HRESULT_CODE(hr));
    }

    return std::error_category::default_error_condition(cond);
  }

  virtual std::string message(int cond) const {
    HRESULT hr = {cond};

    if (FACILITY_WINDOWS == HRESULT_FACILITY(hr)) {
      hr = HRESULT_CODE(hr);
    }

    return error_message(hr);
  }
};

inline
const std::error_category &
com_error_category() noexcept {
  static const com_error_category_cls com_error_category_instance;
  return com_error_category_instance;
}

class com_error : public std::system_error {
public:
  com_error(HRESULT err_code)
    : std::system_error(err_code, com_error_category()) {
    static_assert(sizeof(HRESULT) <= sizeof(int),
		  "can't store HRESULT in an int!");
  }
};

template <class F, class... Args>
HRESULT
check_hresult(F && f, Args && ...args) {
  auto hres = f(std::forward<Args>(args)...);
  if (!SUCCEEDED(hres)) throw com_error(hres);
  return hres;
}

class setupapi_error_category_cls : public std::error_category {
public:
  setupapi_error_category_cls() {}

  virtual const char *name() const noexcept {
    return "setupapi_error";
  }

  std::error_condition
  default_error_condition(int cond) const noexcept {
    HRESULT hr = HRESULT_FROM_SETUPAPI(cond);

    if (FACILITY_WINDOWS == HRESULT_FACILITY(hr)) {
      return windows_error_category().default_error_condition(HRESULT_CODE(hr));
    }

    return std::error_category::default_error_condition(cond);
  }

  virtual std::string message(int cond) const {
    HRESULT hr = HRESULT_FROM_SETUPAPI(cond);

    if (FACILITY_WINDOWS == HRESULT_FACILITY(hr)) {
      hr = HRESULT_CODE(hr);
    }

    return error_message(hr);
  }
};

inline
const std::error_category &
setupapi_error_category() noexcept {
  static const setupapi_error_category_cls setupapi_error_category_instance;
  return setupapi_error_category_instance;
}

class setupapi_error : public std::system_error {
public:
  setupapi_error(DWORD err_code)
    : std::system_error(err_code, setupapi_error_category()) {
    static_assert(sizeof(HRESULT) <= sizeof(int),
		  "can't store HRESULT in an int!");
  }
};

[[noreturn]]
inline
void
throw_setupapi_error() {
  // NB: for some reason on g++ the last error is reset 
  //     when called on the same line as "throw"
  auto err_code = GetLastError();
  throw setupapi_error(err_code);
}

}

#endif

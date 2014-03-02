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

#ifndef _safe_parse_hpp
#define _safe_parse_hpp

#include <encfs/base/optional.h>

#include <cstddef>
#include <cstdint>

#include <stdexcept>
#include <string>
#include <vector>

namespace safe {

template<class TokenStream, class Token>
void
skip_token(TokenStream & stream, Token token) {
  while (true) {
    auto inb = stream.peek();
    if (!inb || *inb != token) break;
    stream.skip();
  }
}

template<class Token, class TokenStream>
std::vector<typename std::decay<decltype(*std::declval<TokenStream>().peek())>::type>
read_token_vector(TokenStream & stream, opt::optional<Token> term,
                  opt::optional<size_t> max_amt, bool drop_term = false) {
  std::vector<typename std::decay<decltype(*std::declval<TokenStream>().peek())>::type> toret;

  for (size_t i = 0; !max_amt || i < *max_amt; ++i) {
    auto mB = stream.peek();
    if (!mB || (term && *mB == *term)) {
      if (drop_term) stream.skip();
      break;
    }
    toret.push_back(*mB);
    stream.skip();
  }
  
  return toret;
}

template<class TokenStream>
std::string
read_string(TokenStream & stream, opt::optional<std::string::value_type> term,
            opt::optional<size_t> max_amt, bool drop_term = false) {
  auto v = read_token_vector(stream, term, max_amt, drop_term);
  return std::string(v.begin(), v.end());
}

template<class TokenStream, class Token>
void
expect(TokenStream & stream, Token expected) {
  auto maybe_token = stream.peek();
  if (!maybe_token || *maybe_token != expected) {
    throw std::runtime_error("Failed expect");
  }
  stream.skip();
}

inline
opt::optional<int8_t>
ascii_digit_value(uint8_t v) {
  const auto ASCII_0 = 48;
  const auto ASCII_9 = 57;
  if (v >= ASCII_0 && v <= ASCII_9) {
    return opt::make_optional((int8_t) (v - ASCII_0));
  }
  return opt::nullopt;
}

template <class IntegerType, class TokenStream>
IntegerType
parse_ascii_integer(TokenStream & stream) {
  if (!stream.peek()) throw std::runtime_error("eof");

  if (!ascii_digit_value(*stream.peek())) {
    throw std::runtime_error("not at a number!");
  }

  IntegerType toret = 0;
  while (true) {
    auto inb = stream.peek();
    if (!inb) break;
    auto digit_val = ascii_digit_value(*inb);
    if (!digit_val) break;

    auto oldtoret = toret;
    toret = toret * 10 + *digit_val;
    if (toret < oldtoret) {
      throw std::runtime_error("overflow occured");
    }

    stream.skip();
  }

  return toret;
}

class BufferParser {
  class BufferStream {
    uint8_t *_ptr;
    uint8_t *_end_ptr;

  public:
    BufferStream(uint8_t *ptr, size_t size)
    : _ptr(ptr)
    , _end_ptr(ptr + size) {}

    opt::optional<uint8_t>
    peek() {
      return (_ptr == _end_ptr) ? opt::nullopt : opt::make_optional(*_ptr);
    }

    void
    skip() {
      if (_ptr != _end_ptr) _ptr += 1;
    }
  };

  BufferStream _stream;

public:
  BufferParser(uint8_t *ptr, size_t size)
  : _stream(ptr, size) {}

  void
  skip_byte(uint8_t byte_) {
    return safe::skip_token(_stream, byte_);
  }

  std::string
  parse_string_until_byte(uint8_t byte_) {
    return safe::read_string(_stream, (char) byte_, opt::nullopt);
  }

  void
  expect(uint8_t byte_)  {
    return safe::expect(_stream, byte_);
  }

  template <class IntegerType>
  IntegerType
  parse_ascii_integer() {
    return safe::parse_ascii_integer<IntegerType>(_stream);
  }
};

}

#endif

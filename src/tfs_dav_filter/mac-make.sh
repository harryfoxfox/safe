#!/bin/sh

export CPPFLAGS=-I/Users/rian/mingw32-custom-root/i686-w64-mingw32/include/ddk
export CXX=i686-w64-mingw32-g++
export CC=i686-w64-mingw32-gcc
export DLLTOOL=i686-w64-mingw32-dlltool

exec make $@

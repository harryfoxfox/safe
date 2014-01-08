# Lockbox: Encrypted File System
# Copyright (C) 2013 Rian Hunter <rian@alum.mit.edu>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>

EXE_NAME := Safe.exe

OUT_DIR ?= out

DAVFUSE_ROOT := $(CURDIR)/../davfuse
ENCFS_ROOT := $(CURDIR)/../encfs
BOTAN_ROOT := $(CURDIR)/encfs-dependencies/botan
TINYXML_ROOT := $(CURDIR)/encfs-dependencies/tinyxml
PROTOBUF_ROOT := $(CURDIR)/../protobuf

HEADERS_ROOT = $(CURDIR)/$(OUT_DIR)/headers
DEPS_INSTALL_ROOT = $(CURDIR)/$(OUT_DIR)/deps

ifdef IS_WIN_CROSS
 WINDRES = $(IS_WIN_CROSS)-windres
 RANLIB = $(IS_WIN_CROSS)-ranlib
 AR = $(IS_WIN_CROSS)-ar
 CC = $(IS_WIN_CROSS)-gcc
 CXX = $(IS_WIN_CROSS)-g++
 DLLTOOL = $(IS_WIN_CROSS)-dlltool
 STRIP = $(IS_WIN_CROSS)-strip
else
 RANLIB ?= ranlib
 WINDRES ?= windres
 DLLTOOL ?= dlltool
 STRIP ?= strip
endif

IS_WIN := $(shell uname | grep -i mingw)

IS_WIN_TARGET = $(or $(IS_WIN_CROSS),$(IS_WIN))
IS_MAC_TARGET = $(if $(IS_WIN_TARGET),,$(shell test `uname` = Darwin && echo 1))

PROCS := $(if $(shell `which nproc 2>/dev/null`),$(shell nproc),$(if $(shell which sysctl),$(shell sysctl hw.ncpu | awk '{print $$2}'),1))

WEBDAV_SERVER_STATIC_LIBRARY = $(DEPS_INSTALL_ROOT)/lib/libwebdav_server_fs.a
ENCFS_STATIC_LIBRARY = $(DEPS_INSTALL_ROOT)/lib/libencfs.a

# we claim to support windows 2000 and above (to be as simple and tight as possible)
# although currently we require comctl32.dll >= v6.0, which is only
# available on minimum windows xp
GLOBAL_WINDOWS_DEFINES = -D_UNICODE -DUNICODE -D_WIN32_IE=0x0600 -D_WIN32_WINNT=0x500 -DWINVER=0x500 -DNTDDI_VERSION=0x05000000

CPPFLAGS_RELEASE = -DNDEBUG
CXXFLAGS_RELEASE = -O3 $(if $(IS_WIN_TARGET),-flto,,)
CFLAGS_RELEASE = -O3 $(if $(IS_WIN_TARGET),-flto,,)
CXXFLAGS_DEBUG = -g
CFLAGS_DEBUG = -g

# default global configuration flags
CPPFLAGS ?= $(if $(RELEASE),$(CPPFLAGS_RELEASE),$(CPPFLAGS_DEBUG))
CXXFLAGS ?= $(if $(RELEASE),$(CXXFLAGS_RELEASE),$(CXXFLAGS_DEBUG))
CFLAGS ?= $(if $(RELEASE),$(CFLAGS_RELEASE),$(CFLAGS_DEBUG))

# these are flags specific to our source files (everything in lockbox-app/src, not our deps)
MY_CPPFLAGS = $(CPPFLAGS) -I$(CURDIR)/src -I$(HEADERS_ROOT) \
 -I$(DEPS_INSTALL_ROOT)/include -I$(DEPS_INSTALL_ROOT)/include/encfs \
 -I$(DEPS_INSTALL_ROOT)/include/encfs/base -I$(DAVFUSE_ROOT)/src \
 $(if $(IS_WIN_TARGET),$(GLOBAL_WINDOWS_DEFINES),)
MY_CXXFLAGS = $(CXXFLAGS) -Wall -Wextra -Werror -std=c++11 \
 $(if $(IS_WIN_TARGET),`$(DEPS_INSTALL_ROOT)/bin/botan-config-1.10 --cflags`,)

# encfs on mac makes use of the Security framework
MAC_EXTRA_LIBRARIES := -framework Security -framework Foundation
WIN_EXTRA_LIBRARIES := -lws2_32
DEPS_LIBRARIES = -lwebdav_server_fs -lencfs \
 $(if $(IS_WIN_TARGET),`$(DEPS_INSTALL_ROOT)/bin/botan-config-1.10 --libs`,) \
 -lprotobuf \
 -ltinyxml
DEPS_EXTRA_LIBRARIES := $(if $(IS_MAC_TARGET),$(MAC_EXTRA_LIBRARIES),) $(if $(IS_WIN_TARGET),$(WIN_EXTRA_LIBRARIES),)

all: test_encfs_main

libwebdav_server_fs: clean
	@rm -rf "$(DAVFUSE_ROOT)/out"
	@cd $(DAVFUSE_ROOT); rm config.mk; cp $(if $(IS_WIN_TARGET),config-nt-msvcrt-mingw.mk,config-xnu-bsdlibc-clang.mk) config.mk
	@cd $(DAVFUSE_ROOT); AR="$(AR)" CC="$(CC)" CXX="$(CXX)" CPPFLAGS="$(CPPFLAGS)" CXXFLAGS="$(CXXFLAGS)" CFLAGS="$(CFLAGS)" make -j$(PROCS) RELEASE= USE_DYNAMIC_FS=1 libwebdav_server_fs.a
	@mkdir -p $(DEPS_INSTALL_ROOT)/lib
	@cp "$(DAVFUSE_ROOT)/out/targets/$(notdir $(WEBDAV_SERVER_STATIC_LIBRARY))" $(WEBDAV_SERVER_STATIC_LIBRARY)
	@mkdir -p $(DEPS_INSTALL_ROOT)/include/davfuse
	@find $(DAVFUSE_ROOT)/src -name '*.h' | (while read F; do cp $$F $(DEPS_INSTALL_ROOT)/include/davfuse/$$(basename $$F); done)
	@find $(DAVFUSE_ROOT)/out/libwebdav_server_fs/headers -name '*.h' | (while read F; do cp $$F $(DEPS_INSTALL_ROOT)/include/davfuse/$$(basename $$F); done)

libencfs: clean
#	TODO: don't require fuse when configuring encfs
	@cd $(ENCFS_ROOT); rm -rf CMakeCache.txt CMakeFiles
	@cd $(ENCFS_ROOT); \
         cmake . -DCMAKE_BUILD_TYPE=Debug \
               $(if $(AR),-DCMAKE_AR=`which $(AR)`,) \
               $(if $(RANLIB),-DCMAKE_RANLIB=`which $(RANLIB)`,) \
               $(if $(CXX),-DCMAKE_CXX_COMPILER=$(CXX),) \
               $(if $(CC),-DCMAKE_C_COMPILER=$(CC),) \
               $(if $(IS_WIN),-G"MSYS Makefiles",) \
               -DCMAKE_PREFIX_PATH=$(DEPS_INSTALL_ROOT) \
               "-DCMAKE_CXX_FLAGS=$(CPPFLAGS) $(CXXFLAGS)" \
               "-DCMAKE_C_FLAGS=$(CPPFLAGS) $(CFLAGS)" \
               "-DCMAKE_BUILD_TYPE=None" \
               $(if $(IS_WIN_TARGET),-DCMAKE_INCLUDE_PATH=$(DEPS_INSTALL_ROOT)/include/botan-1.10,)
	@cd $(ENCFS_ROOT); make clean
	@cd $(ENCFS_ROOT); PATH="$(DEPS_INSTALL_ROOT)/bin:$$PATH" make -j$(PROCS) encfs-base encfs-cipher encfs-fs
#	copy all encfs headers into our build headers dir
	@find $(ENCFS_ROOT) -name '*.h' | (while read F; do ROOT=$(ENCFS_ROOT); NEWF=$$(echo $$F | sed "s|^$$ROOT|$(OUT_DIR)/deps/include/encfs|"); mkdir -p $$(dirname "$$NEWF"); cp $$F $$NEWF; done)
#       create archive
# TODO: it would probably be better if the encfs build system did this
	@mkdir -p $(DEPS_INSTALL_ROOT)/lib
	@TMPDIR=`mktemp -d /tmp/temp.XXXX`; \
         cd $$TMPDIR; \
         $(AR) -x $(ENCFS_ROOT)/cipher/libencfs-cipher.a; \
         $(AR) -x $(ENCFS_ROOT)/fs/libencfs-fs.a; \
         $(AR) -x $(ENCFS_ROOT)/base/libencfs-base.a; rm -f $(ENCFS_STATIC_LIBRARY); $(AR) rcs $(ENCFS_STATIC_LIBRARY) *.*;

libbotan: clean
	@mkdir -p /tmp/botan_path;
	@echo '#!/bin/sh' > /tmp/botan_path/ar; \
         echo "export PATH=\"$$PATH\"" >> /tmp/botan_path/ar; \
         echo 'exec $(AR) $$@' >> /tmp/botan_path/ar; \
         chmod a+x /tmp/botan_path/ar;
	@echo '#!/bin/sh' > /tmp/botan_path/ranlib; \
         echo "export PATH=\"$$PATH\"" >> /tmp/botan_path/ranlib; \
         echo 'exec $(RANLIB) $$@' >> /tmp/botan_path/ranlib; \
         chmod a+x /tmp/botan_path/ranlib;
	@echo '#!/bin/sh' > /tmp/botan_path/c++; \
         echo "export PATH=\"$$PATH\"" >> /tmp/botan_path/c++; \
         echo 'exec $(CXX) $(CPPFLAGS) $(CXXFLAGS) $$@' >> /tmp/botan_path/c++; \
         chmod a+x /tmp/botan_path/c++;
# the nr module has an issue with LTO
	@cd $(BOTAN_ROOT); PATH="/tmp/botan_path:$$PATH" ./configure.py \
	--disable-modules=nr \
         --no-optimizations --prefix=$(DEPS_INSTALL_ROOT) \
         --disable-shared \
         $(if $(shell test $(CXX) == clang++ && echo 1),--cc=clang,--cc-bin=c++) \
         $(if $(IS_WIN_TARGET),--os=mingw --cpu=x86,) --with-tr1=none
	@cd $(BOTAN_ROOT); PATH="/tmp/botan_path:$$PATH" make clean
	@cd $(BOTAN_ROOT); PATH="/tmp/botan_path:$$PATH" make -j$(PROCS)
	@cd $(BOTAN_ROOT); PATH="/tmp/botan_path:$$PATH" make install

libprotobuf: clean
	@cd $(PROTOBUF_ROOT); if [ ! -e configure ]; then ./autogen.sh; fi
# first build protobuf protoc
	@cd $(PROTOBUF_ROOT); unset CC CXX CPPFLAGS CFLAGS CXXFLAGS AR RANLIB; ./configure --prefix=$(DEPS_INSTALL_ROOT) $(if $(IS_WIN_CROSS),--target $(IS_WIN_CROSS),) --disable-shared
	@cd $(PROTOBUF_ROOT); unset CC CXX CPPFLAGS CFLAGS CXXFLAGS AR RANLIB; make clean
	@cd $(PROTOBUF_ROOT); cd src; unset CC CXX CPPFLAGS CXXFLAGS AR RANLIB; make -j$(PROCS) $(if $(IS_WIN),protoc.exe,protoc)
	@mkdir -p $(DEPS_INSTALL_ROOT)/bin
	@cp $(PROTOBUF_ROOT)/src/$(if $(IS_WIN),protoc.exe,protoc) $(DEPS_INSTALL_ROOT)/bin
# now build libprotobuf.a
	@cd $(PROTOBUF_ROOT); AR="$(AR)" CC="$(CC)" CXX="$(CXX)" CPPFLAGS="$(CPPFLAGS)" CXXFLAGS="$(CXXFLAGS)" CFLAGS="$(CFLAGS)" ./configure --prefix=$(DEPS_INSTALL_ROOT) --with-protoc=$(DEPS_INSTALL_ROOT)/bin/$(if $(IS_WIN),protoc.exe,protoc) $(if $(IS_WIN_CROSS),--host $(IS_WIN_CROSS),) --disable-shared
	@cd $(PROTOBUF_ROOT); AR="$(AR)" CC="$(CC)" CXX="$(CXX)" CPPFLAGS="$(CPPFLAGS)" CXXFLAGS="$(CXXFLAGS)" CFLAGS="$(CFLAGS)" make clean
	@cd $(PROTOBUF_ROOT); AR="$(AR)" CC="$(CC)" CXX="$(CXX)" CPPFLAGS="$(CPPFLAGS)" CXXFLAGS="$(CXXFLAGS)" CFLAGS="$(CFLAGS)" make -j$(PROCS)
	@cd $(PROTOBUF_ROOT); AR="$(AR)" CC="$(CC)" CXX="$(CXX)" CPPFLAGS="$(CPPFLAGS)" CXXFLAGS="$(CXXFLAGS)" CFLAGS="$(CFLAGS)" make install

libtinyxml: clean
	@mkdir -p $(DEPS_INSTALL_ROOT)/lib
	@mkdir -p $(DEPS_INSTALL_ROOT)/include
	@cd $(TINYXML_ROOT); rm -f libtinyxml.a tinystr.o  tinyxml.o  tinyxmlerror.o  tinyxmlparser.o
	cd $(TINYXML_ROOT); \
 $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c tinystr.cpp  tinyxml.cpp  tinyxmlerror.cpp  tinyxmlparser.cpp
	@cd $(TINYXML_ROOT); $(AR) rcs libtinyxml.a tinystr.o  tinyxml.o  tinyxmlerror.o  tinyxmlparser.o
	@cd $(TINYXML_ROOT); mv libtinyxml.a $(DEPS_INSTALL_ROOT)/lib
	@cd $(TINYXML_ROOT); cp tinyxml.h tinystr.h $(DEPS_INSTALL_ROOT)/include

NLSCHECK := $(CURDIR)/$(OUT_DIR)/deps/include/lockbox_nlscheck.h
$(NLSCHECK):
	@mkdir -p /tmp/nlschk && \
         echo '#include <windows.h>' > /tmp/nlschk/chk.c && \
         echo 'NORM_FORM a = NormalizationC;' >> /tmp/nlschk/chk.c && \
	 rm -f /tmp/nlschk/chk.o && \
         $(CC) -o /tmp/nlschk/chk.o -c /tmp/nlschk/chk.c 2>/dev/null >/dev/null; \
         echo > $(NLSCHECK); \
         if [ -e /tmp/nlschk/chk.o ]; then \
         echo '#define LOCKBOX_HAVE_WINNLS' >> $(NLSCHECK); \
         fi

nlscheck: $(NLSCHECK)

NORMALIZ_DEP := $(CURDIR)/$(OUT_DIR)/deps/lib/libnormaliz.a
$(NORMALIZ_DEP): $(CURDIR)/normaliz.def
	$(DLLTOOL) -k -d $^ -l $@

normaliz: $(NORMALIZ_DEP)

dependencies: libprotobuf libtinyxml \
 $(if $(IS_WIN_TARGET),libbotan,) \
 libencfs \
 libwebdav_server_fs \
 $(if $(IS_WIN_TARGET),normaliz nlscheck,)

clean-deps:
	rm -rf $(OUT_DIR)

clean:
	rm -f src/lockbox/*.o

SRCS = fs_fsio.cpp CFsToFsIO.cpp webdav_server.cpp fs.cpp \
	SecureMemPasswordReader.cpp UnicodeWrapperFsIO.cpp \
	$(if $(IS_WIN_TARGET),unicode_fs_win.cpp,) \
	$(if $(IS_MAC_TARGET),unicode_fs_mac.mm,)

TEST_ENCFS_MAIN_SRCS = test_encfs_main.cpp $(SRCS)
TEST_ENCFS_MAIN_OBJS = $(patsubst %,src/lockbox/%.o,${TEST_ENCFS_MAIN_SRCS})

WINDOWS_APP_MAIN_SRCS = app_main_win.cpp app_win.rc mount_win.cpp \
 windows_gui_util.cpp about_dialog_win.cpp \
 create_lockbox_dialog_win.cpp create_lockbox_dialog_logic.cpp \
 mount_lockbox_dialog_win.cpp mount_lockbox_dialog_logic.cpp \
 windows_menu.cpp welcome_dialog_win.cpp \
 $(SRCS)
WINDOWS_APP_MAIN_OBJS = $(patsubst %,src/lockbox/%.o,${WINDOWS_APP_MAIN_SRCS})

# dependencies

src/lockbox/*.o: GNUmakefile src/lockbox/*.hpp src/lockbox/*.h

src/lockbox/windows_app.rc.o: src/lockbox/windows_app.rc \
	src/lockbox/windows_app.manifest

test_encfs_main: $(TEST_ENCFS_MAIN_OBJS) $(ENCFS_STATIC_LIBRARY) \
	$(WEBDAV_SERVER_STATIC_LIBRARY) GNUmakefile

$(EXE_NAME): $(WINDOWS_APP_MAIN_OBJS) $(ENCFS_STATIC_LIBRARY) \
	$(WEBDAV_SERVER_STATIC_LIBRARY) GNUmakefile

# build instructions

%.cpp.o: %.cpp
	$(CXX) $(MY_CPPFLAGS) $(MY_CXXFLAGS) -c -o $@ $<

%.mm.o: %.mm
	$(CXX) $(MY_CPPFLAGS) $(MY_CXXFLAGS) -c -o $@ $<

%.rc.o: %.rc
	$(WINDRES) -I./src -i $< -o $@

test_encfs_main:
	$(CXX) -L$(DEPS_INSTALL_ROOT)/lib $(MY_CXXFLAGS) \
 -o $@ $(TEST_ENCFS_MAIN_OBJS) $(DEPS_LIBRARIES) $(DEPS_EXTRA_LIBRARIES)

ASLR_LINK_FLAGS := -Wl,--dynamicbase=true -Wl,--nxcompat=true
WINDOWS_SUBSYS_LINK_FLAGS := -mwindows

$(EXE_NAME):
	$(CXX) $(ASLR_LINK_FLAGS) $(WINDOWS_SUBSYS_LINK_FLAGS) -static \
 -L$(DEPS_INSTALL_ROOT)/lib $(MY_CXXFLAGS) -o $@.pre $(WINDOWS_APP_MAIN_OBJS) \
 $(DEPS_LIBRARIES) $(DEPS_EXTRA_LIBRARIES) \
 -lole32 -lcomctl32 -lnormaliz
	$(if $(RELEASE),$(STRIP) -s $@.pre,)
	$(if $(RELEASE),upx --best --all-methods --ultra-brute $@.pre,)
	mv $@.pre $@

.PHONY: dependencies clean libbotan \
	libprotobuf libtinyxml libencfs libwebdav_server_fs nlscheck normaliz clean-deps

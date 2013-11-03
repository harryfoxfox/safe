DAVFUSE_ROOT := $(CURDIR)/../davfuse
ENCFS_ROOT := $(CURDIR)/../encfs
BOTAN_ROOT := $(CURDIR)/encfs-dependencies/botan
TINYXML_ROOT := $(CURDIR)/encfs-dependencies/tinyxml
GLOG_ROOT := $(CURDIR)/../google-glog
PROTOBUF_ROOT := $(CURDIR)/../protobuf
HEADERS_ROOT := $(CURDIR)/out/headers
DEPS_INSTALL_ROOT := $(CURDIR)/out/deps
PROCS := $(if $(shell `which nproc 2>/dev/null`),$(shell nproc),1)

IS_WIN := $(shell uname | grep -i mingw)
IS_MAC := $(shell test `uname` = Darwin && echo 1)

WEBDAV_SERVER_STATIC_LIBRARY = $(DEPS_INSTALL_ROOT)/lib/libwebdav_server_fs.a
ENCFS_STATIC_LIBRARY = $(DEPS_INSTALL_ROOT)/lib/libencfs.a

# we claim to support windows 2000 and above (to be as simple and tight as possible)
# although currently we require comctl32.dll >= v6.0, which is only
# available on minimum windows xp
GLOBAL_WINDOWS_DEFINES = -D_UNICODE -DUNICODE -D_WIN32_IE=0x0600 -D_WIN32_WINNT=0x500 -DWINVER=0x500 -DNTDDI_VERSION=0x05000000

MY_CPPFLAGS = $(CPPFLAGS) -I$(CURDIR)/src -I$(HEADERS_ROOT) \
 -I$(DEPS_INSTALL_ROOT)/include -I$(DEPS_INSTALL_ROOT)/include/encfs \
 -I$(DEPS_INSTALL_ROOT)/include/encfs/base -I$(DAVFUSE_ROOT)/src \
 $(if $(IS_WIN),$(GLOBAL_WINDOWS_DEFINES),)
MY_CXXFLAGS = $(CXXFLAGS) -g -Wall -Wextra -Werror -std=c++11

# encfs on mac makes use of the Security framework
MAC_EXTRA_LIBRARIES := -framework Security
WIN_EXTRA_LIBRARIES := -lws2_32
DEPS_EXTRA_LIBRARIES := $(if $(IS_MAC),$(MAC_EXTRA_LIBRARIES),) $(if $(IS_WIN),$(WIN_EXTRA_LIBRARIES),)

all: test_encfs_main

libwebdav_server_fs: clean
	@rm -rf "$(DAVFUSE_ROOT)/out"
	@cd $(DAVFUSE_ROOT); make -j$(PROCS) USE_DYNAMIC_FS=1 libwebdav_server_fs.a
	@mkdir -p $(DEPS_INSTALL_ROOT)/lib
	@cp "$(DAVFUSE_ROOT)/out/targets/$(notdir $(WEBDAV_SERVER_STATIC_LIBRARY))" $(WEBDAV_SERVER_STATIC_LIBRARY)
	@mkdir -p $(DEPS_INSTALL_ROOT)/include/davfuse
	@find $(DAVFUSE_ROOT) -name '*.h' | (while read F; do cp $$F $(DEPS_INSTALL_ROOT)/include/davfuse/$$(basename $$F); done)

libencfs: clean
#	TODO: don't require fuse when configuring encfs
	@cd $(ENCFS_ROOT); rm -rf CMakeCache.txt CMakeFiles
	@cd $(ENCFS_ROOT); CXXFLAGS="$(CXXFLAGS) -DGOOGLE_GLOG_DLL_DECL=''" cmake . -DCMAKE_BUILD_TYPE=Debug $(if $(IS_WIN),-G"MSYS Makefiles",) -DCMAKE_PREFIX_PATH=$(DEPS_INSTALL_ROOT) $(if $(IS_WIN),-DCMAKE_INCLUDE_PATH=$(DEPS_INSTALL_ROOT)/include/botan-1.10,)
	@cd $(ENCFS_ROOT); make clean
	@cd $(ENCFS_ROOT); make -j$(PROCS) encfs-base encfs-cipher encfs-fs
#	copy all encfs headers into our build headers dir
	@find $(ENCFS_ROOT) -name '*.h' | (while read F; do ROOT=$(ENCFS_ROOT); NEWF=$$(echo $$F | sed "s|^$$ROOT|out/deps/include/encfs|"); mkdir -p $$(dirname "$$NEWF"); cp $$F $$NEWF; done)
#       create archive
# TODO: it would probably be better if the encfs build system did this
	@mkdir -p $(DEPS_INSTALL_ROOT)/lib
	@TMPDIR=`mktemp -d /tmp/temp.XXXX`; \
         cd $$TMPDIR; \
         ar -x $(ENCFS_ROOT)/cipher/libencfs-cipher.a; \
         ar -x $(ENCFS_ROOT)/fs/libencfs-fs.a; \
         ar -x $(ENCFS_ROOT)/base/libencfs-base.a; ar rcs $(ENCFS_STATIC_LIBRARY) *.*;

libbotan: clean
	@cd $(BOTAN_ROOT); ./configure.py --prefix=$(DEPS_INSTALL_ROOT) --disable-shared $(if $(shell test $(CXX) == clang++ && echo 1),--cc=clang,) $(if $(IS_WIN),--os=mingw --cpu=x86,)
	@cd $(BOTAN_ROOT); make clean
	@cd $(BOTAN_ROOT); make -j$(PROCS)
	@cd $(BOTAN_ROOT); make install

libglog: clean
	@cd $(GLOG_ROOT); CXXFLAGS="$(CXXFLAGS) -DGOOGLE_GLOG_DLL_DECL=''" ./configure --prefix=$(DEPS_INSTALL_ROOT) --disable-shared
	@cd $(GLOG_ROOT); make clean
	@cd $(GLOG_ROOT); make -j$(PROCS)
	@cd $(GLOG_ROOT); make install

libprotobuf: clean
	@cd $(PROTOBUF_ROOT); if [ ! -e configure ]; then ./autogen.sh; fi
	@cd $(PROTOBUF_ROOT); CXXFLAGS="$(CXXFLAGS) -DGOOGLE_GLOG_DLL_DECL=''" ./configure --prefix=$(DEPS_INSTALL_ROOT) --disable-shared
	@cd $(PROTOBUF_ROOT); make clean
	@cd $(PROTOBUF_ROOT); make -j$(PROCS)
	@cd $(PROTOBUF_ROOT); make install

libtinyxml: clean
	@cd $(TINYXML_ROOT); rm -f libtinyxml.a tinystr.o  tinyxml.o  tinyxmlerror.o  tinyxmlparser.o
	@cd $(TINYXML_ROOT); $(CXX) $(CXXFLAGS) -c tinystr.cpp  tinyxml.cpp  tinyxmlerror.cpp  tinyxmlparser.cpp
	@cd $(TINYXML_ROOT); ar rcs libtinyxml.a tinystr.o  tinyxml.o  tinyxmlerror.o  tinyxmlparser.o
	@cd $(TINYXML_ROOT); mv libtinyxml.a $(DEPS_INSTALL_ROOT)/lib
	@cd $(TINYXML_ROOT); cp tinyxml.h tinystr.h $(DEPS_INSTALL_ROOT)/include

WINHTTP_DEP := $(CURDIR)/out/deps/lib/libwinhttp.a
$(WINHTTP_DEP): $(CURDIR)/winhttp.def
	dlltool -k -d winhttp.def -l $(CURDIR)/out/deps/lib/libwinhttp.a

WINHTTP_DEP2 := $(CURDIR)/out/deps/include/winhttp.h
$(WINHTTP_DEP2): $(CURDIR)/winhttp.h
	cp $(CURDIR)/winhttp.h out/deps/include/winhttp.h

winhttp: $(WINHTTP_DEP2) $(WINHTTP_DEP)

dependencies: libglog libbotan libprotobuf libtinyxml libencfs \
 libwebdav_server_fs \
 $(if $(IS_WIN),winhttp,)

clean-deps:
	rm -rf out

clean:
	rm -f src/lockbox/*.o

SRCS = fs_fsio.cpp CFsToFsIO.cpp lockbox_server.cpp SecureMemPasswordReader.cpp

TEST_ENCFS_MAIN_SRCS = test_encfs_main.cpp $(SRCS)
TEST_ENCFS_MAIN_OBJS = $(patsubst %,src/lockbox/%.o,${TEST_ENCFS_MAIN_SRCS})

WINDOWS_APP_MAIN_SRCS = windows_app_main.cpp windows_app_main.rc $(SRCS)
WINDOWS_APP_MAIN_OBJS = $(patsubst %,src/lockbox/%.o,${WINDOWS_APP_MAIN_SRCS})

# dependencies

src/lockbox/*.o: Makefile src/lockbox/*.hpp src/lockbox/*.h

src/lockbox/windows_app_main.rc.o: src/lockbox/windows_app_main.rc \
	src/lockbox/windows_app_main.manifest

test_encfs_main: $(TEST_ENCFS_MAIN_OBJS) $(ENCFS_STATIC_LIBRARY) \
	$(WEBDAV_SERVER_STATIC_LIBRARY) Makefile

Lockbox.exe: $(WINDOWS_APP_MAIN_OBJS) $(ENCFS_STATIC_LIBRARY) \
	$(WEBDAV_SERVER_STATIC_LIBRARY) Makefile

# build instructions

%.cpp.o: %.cpp
	$(CXX) `$(DEPS_INSTALL_ROOT)/bin/botan-config-1.10 --cflags` $(MY_CXXFLAGS) $(MY_CPPFLAGS) -c -o $@ $<

%.rc.o: %.rc
	windres -I.\src\lockbox -i $< -o $@

test_encfs_main:
	$(CXX) -O4 -L$(DEPS_INSTALL_ROOT)/lib $(MY_CXXFLAGS) \
 -o $@ $(TEST_ENCFS_MAIN_OBJS) \
 -lwebdav_server_fs -lencfs \
 `$(DEPS_INSTALL_ROOT)/bin/botan-config-1.10 --libs` -lprotobuf \
 -lglog  -ltinyxml $(DEPS_EXTRA_LIBRARIES)

ASLR_LINK_FLAGS := -Wl,--dynamicbase=true -Wl,--nxcompat=true
WINDOWS_SUBSYS_LINK_FLAGS := -mwindows

Lockbox.exe:
	$(CXX) $(ASLR_LINK_FLAGS) $(WINDOWS_SUBSYS_LINK_FLAGS) -static \
 -O4 -L$(DEPS_INSTALL_ROOT)/lib $(MY_CXXFLAGS) -o $@ $(WINDOWS_APP_MAIN_OBJS) \
 -lwebdav_server_fs -lencfs \
 `$(DEPS_INSTALL_ROOT)/bin/botan-config-1.10 --libs` -lprotobuf \
 -lglog  -ltinyxml -lole32 -lcomctl32 -lwinhttp $(DEPS_EXTRA_LIBRARIES)

.PHONY: dependencies clean libglog libbotan \
	libprotobuf libtinyxml libencfs libwebdav_server_fs winhttp

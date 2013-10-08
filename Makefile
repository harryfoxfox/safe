DAVFUSE_ROOT := $(CURDIR)/../davfuse
ENCFS_ROOT := $(CURDIR)/../encfs
HEADERS_ROOT := $(CURDIR)/out/headers
DEPS_INSTALL_ROOT := $(CURDIR)/out/deps

WEBDAV_SERVER_STATIC_LIBRARY = $(DEPS_INSTALL_ROOT)/lib/libwebdav_server_sockets_fs.a
ENCFS_STATIC_LIBRARY = $(DEPS_INSTALL_ROOT)/lib/libencfs.a

CXX_FLAGS = -std=c++11 -stdlib=libc++ -I/Users/rian/encfs-dev-prefix/include -I$(HEADERS_ROOT) -I$(DEPS_INSTALL_ROOT)/include -I$(DEPS_INSTALL_ROOT)/include/encfs -I$(DEPS_INSTALL_ROOT)/include/encfs/base -I$(DAVFUSE_ROOT)/src

$(WEBDAV_SERVER_STATIC_LIBRARY):
	@make -C  "$(DAVFUSE_ROOT)" "$(notdir $(WEBDAV_SERVER_STATIC_LIBRARY))"

# TODO: add all *.h and *.cpp files the library depends on
#       or make this a phony target and attempt to build everytime
#       (relying on the build system to rebuild)
$(ENCFS_STATIC_LIBRARY):
	@rm -fr $(DEPS_INSTALL_ROOT)/include/encfs
	@rm -fr $(ENCFS_STATIC_LIBRARY)
#	TODO: don't require fuse when configuring encfs
	@cmake $(ENCFS_ROOT) -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/Users/rian/encfs-dev-prefix/ -DCMAKE_CXX_FLAGS=-stdlib=libc++
	@make -C "$(ENCFS_ROOT)" clean
	@make -C "$(ENCFS_ROOT)" -j6 encfs-base encfs-cipher encfs-fs
#	copy all encfs headers into our build headers dir
	@find -X $(ENCFS_ROOT) -name '*.h' | (while read F; do ROOT=$(ENCFS_ROOT); NEWF=$$(echo $$F | sed "s|^$$ROOT|out/deps/include/encfs|"); mkdir -p $$(dirname "$$NEWF"); cp $$F $$NEWF; done)
#       create archive
# TODO: it would probably be better if the encfs build system did this
	@mkdir -p $(DEPS_INSTALL_ROOT)/lib
	@TMPDIR=`mktemp -d /tmp/temp.XXXX`; \
         cd $$TMPDIR; \
         ar -x $(ENCFS_ROOT)/cipher/libencfs-cipher.a; \
         ar -x $(ENCFS_ROOT)/fs/libencfs-fs.a; \
         ar -x $(ENCFS_ROOT)/base/libencfs-base.a; ar rcs $(ENCFS_STATIC_LIBRARY) *.o;

libencfs.a: $(ENCFS_STATIC_LIBRARY)

# TODO: don't hardcode this
FS_IMPL=posix

$(HEADERS_ROOT)/c_fs_to_fs_io_fs.h: $(DAVFUSE_ROOT)/generate-interface-implementation.sh
	@mkdir -p $(dir $@)
	C_FS_TO_FS_IO_FS_DEF=c_fs_to_fs_io/fs/$(FS_IMPL) sh $(DAVFUSE_ROOT)/generate-interface-implementation.sh c_fs_to_fs_io_fs $(DAVFUSE_ROOT)/src/fs.idef > $@

src/fs_FsIO.o: src/fs_FsIO.cpp $(ENCFS_STATIC_LIBRARY)# $(WEBDAV_SERVER_STATIC_LIBRARY)
	$(CXX) $(CXX_FLAGS) -c -o $@ src/fs_FsIO.cpp

src/CFsToFsIO.o: src/CFsToFsIO.cpp $(HEADERS_ROOT)/c_fs_to_fs_io_fs.h $(ENCFS_STATIC_LIBRARY)# $(WEBDAV_SERVER_STATIC_LIBRARY)
	$(CXX) $(CXX_FLAGS) -c -o $@ src/CFsToFsIO.cpp


.PHONY: libencfs.a
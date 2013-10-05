DAVFUSE_ROOT = ../davfuse
ENCFS_ROOT = ../encfs

WEBDAV_SERVER_STATIC_LIBRARY = $(DAVFUSE_ROOT)/out/targets/libwebdav_server_sockets_fs.a
ENCFS_STATIC_LIBRARY = $(ENCFS_ROOT)/fs/libencfs.a

$(WEBDAV_SERVER_STATIC_LIBRARY):
	@make -C "$(DAVFUSE_ROOT)" "$(basename $(WEBDAV_SERVER_STATIC_LIBRARY))"

$(ENCFS_STATIC_LIBRARY):
	@make -C "$(ENCFS_ROOT)/fs" "$(basename $(ENCFS_STATIC_LIBRARY))"

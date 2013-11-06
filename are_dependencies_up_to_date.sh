#!/bin/sh

DEPS_OUT_OF_DATE_RETURN_CODE=1
DEPS_UP_TO_DATE_RETURN_CODE=0

APPDIR="$1"
if [ -z "$APPDIR" ]; then
    APPDIR="."
fi

# just check libs for now
# TODO: check headers as well!

ANYGLOG_FILE=$(find "$APPDIR/../google-glog" \
    -newer "$APPDIR/out/deps/lib/libglog.a" | \
    head -n 1 2>/dev/null)
if [ ! -e "$APPDIR/out/deps/lib/libglog.a" ] || [ "$ANYGLOG_FILE" ]; then
    echo "google-glog is out of date" >> /dev/stderr
    exit $DEPS_OUT_OF_DATE_RETURN_CODE
fi

ANYBOTAN_FILE=$(find "$APPDIR/encfs-dependencies/botan" \
    -newer "$APPDIR/out/deps/lib/libbotan-1.10.a" | \
    head -n 1 2>/dev/null)
if [ ! -e "$APPDIR/out/deps/lib/libbotan-1.10.a" ] || [ "$ANYBOTAN_FILE" ]; then
    echo "botan is out of date" >> /dev/stderr
    exit $DEPS_OUT_OF_DATE_RETURN_CODE
fi

ANYPROTOBUF_FILE=$(find "$APPDIR/../protobuf" \
    -newer "$APPDIR/out/deps/lib/libprotobuf.a" | \
    head -n 1 2>/dev/null)
if [ ! -e "$APPDIR/out/deps/lib/libprotobuf.a" ] || [ "$ANYPROTOBUF" ]; then
    echo "protobuf is out of date" >> /dev/stderr
    exit $DEPS_OUT_OF_DATE_RETURN_CODE
fi

ANYTINYXML_FILE=$(find "$APPDIR/encfs-dependencies/tinyxml" \
    -newer "$APPDIR/out/deps/lib/libtinyxml.a" | \
    head -n 1 2>/dev/null)
if [ ! -e "$APPDIR/out/deps/lib/libtinyxml.a" ] || [ "$ANYTINYXML_FILE" ]; then
    echo "tinyxml is out of date" >> /dev/stderr
    exit $DEPS_OUT_OF_DATE_RETURN_CODE
fi

ANYENCFS_FILE=$(find "$APPDIR/../encfs" \
    -newer "$APPDIR/out/deps/lib/libencfs.a" | \
    head -n 1 2>/dev/null)
if [ ! -e "$APPDIR/out/deps/lib/libencfs.a" ] || [ "$ANYENCFS_FILE" ]; then
    echo "encfs is out of date" >> /dev/stderr
    exit $DEPS_OUT_OF_DATE_RETURN_CODE
fi

ANYWEBDAV_SERVER_FS_FILE=$(find "$APPDIR/../davfuse" \
    -newer "$APPDIR/out/deps/lib/libwebdav_server_fs.a" | \
    head -n 1 2>/dev/null)
if [ ! -e "$APPDIR/out/deps/lib/libwebdav_server_fs.a" ] || [ "$ANYWEBDAV_SERVER_FS_FILE" ]; then
    echo "davfuse is out of date" >> /dev/stderr
    exit $DEPS_OUT_OF_DATE_RETURN_CODE
fi

exit $DEPS_UP_TO_DATE_RETURN_CODE

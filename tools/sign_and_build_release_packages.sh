#!/bin/bash

# This script builds Safe for Mac OS X and also cross-builds Safe for Windows
# NB: it is currently somewhat hardcoded to the main developer's environment
# TODO: A higher-level script to:
# 1. tag repo
# 2. upload tag to github
# 3. build release products (this script)
# 4. package products (this script)
# 5. sign packages (this script)
# 6. create release w/ notes on github
# 7. upload packages and signatures to github
# 8. update website download links to point to new version

set -e

MAC_APP_SIGNING_IDENTITY="Developer ID Application: Rian Hunter (V846G4FW34)"

DIR_OF_SCRIPT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

get_version() {
    VERSION_TEMP=$(mktemp -t safe_build)
    VERSION_TEMP_OUT=$(mktemp -t safe_build)
    cat <<EOF > "$VERSION_TEMP"
#include <safe/version.h>
#include <stdio.h>
int main() { printf("%s\n", SAFE_VERSION_STR); return 0; }
EOF
    cc "-I${DIR_OF_SCRIPT}/../src" "-DNDEBUG" -xc "$VERSION_TEMP" -o "$VERSION_TEMP_OUT"

    "$VERSION_TEMP_OUT"
}

# Get credentials to complete build

if [ -z "$SAFE_PFX_PATH" ]; then
    printf 'Path to pfx file:'
    read SAFE_PFX_PATH
fi

if [ -z "$SAFE_PFX_PASSWORD" ]; then
    stty -echo
    printf 'Safe pfx password:'
    read SAFE_PFX_PASSWORD
    stty echo
fi

# Provoke gpg to ask for key password (for signing later)
gpg --export-secret-subkeys > /dev/null

# Okay now that we have all the credentials let's build!

VERSION=$(get_version)

# Set up output directory
DIR_OF_PRODUCTS="$DIR_OF_SCRIPT/../release-builds/Safe-$VERSION"
rm -rf "$DIR_OF_PRODUCTS"
mkdir -p "$DIR_OF_PRODUCTS"

# Build for Mac OS X
unset IS_WIN_CROSS
cd $DIR_OF_SCRIPT/../Xcode
ARCHIVE_PATH="$TMPDIR/safe-archive.xcarchive"
rm -rf "$ARCHIVE_PATH"
xcodebuild -scheme Safe -archivePath "$ARCHIVE_PATH" archive

# Create release package
xcodebuild -exportArchive -exportFormat APP -archivePath "$ARCHIVE_PATH" -exportPath "$DIR_OF_PRODUCTS/Safe.app" -exportSigningIdentity "$MAC_APP_SIGNING_IDENTITY"
cd "$DIR_OF_PRODUCTS"
# TODO: verify app before zipping: spctl -a -v Safe.app
zip -r -9 Safe-$VERSION-MacOSX.zip Safe.app

# Create debug package
MACOSX_DEBUG_DIR="$DIR_OF_PRODUCTS/Safe-$VERSION-MacOSX-Debug"
mkdir -p "$MACOSX_DEBUG_DIR"
mv ~/Safe-Unstripped "$MACOSX_DEBUG_DIR"
mv "$ARCHIVE_PATH/dSYMS" "$MACOSX_DEBUG_DIR"
cd $(dirname "$MACOSX_DEBUG_DIR")
zip -r -9 Safe-$VERSION-MacOSX-Debug.zip $(basename "$MACOSX_DEBUG_DIR")

# Build for Windows
cd $DIR_OF_SCRIPT/..
export IS_WIN_CROSS=i686-w64-mingw32
export SAFE_PFX_PATH
export SAFE_PFX_PASSWORD
make clean-deps clean
make RELEASE=1 dependencies
make RELEASE=1 -j4 Safe.exe

# Create debug and release packages
mv Safe.exe "$DIR_OF_PRODUCTS"
mv Safe-Debug.exe "$DIR_OF_PRODUCTS"
cd "$DIR_OF_PRODUCTS"
zip -r -9 Safe-$VERSION-Windows-Debug.zip Safe-Debug.exe
zip -r -9 Safe-$VERSION-Windows.zip Safe.exe

# Now sign each package
for I in *.zip; do
    gpg --detach-sign "$I"
done

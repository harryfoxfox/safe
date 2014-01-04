#!/bin/sh

set -e

DIR=$(mktemp -d -t createicns)

cp logo-16-color.png "$DIR/icon_16x16.png"
cp logo-32.png "$DIR/icon_32x32.png"
cp logo-48.png "$DIR/icon_48x48.png"
cp logo-64.png "$DIR/icon_32x32@2x.png"
cp logo-128.png "$DIR/icon_128x128.png"
cp logo-256.png "$DIR/icon_256x256.png"
cp logo-512.png "$DIR/icon_512x512.png"
cp logo-1024.png "$DIR/icon_512x512@2x.png"

mv "$DIR" "$DIR.iconset"

iconutil -c icns "$DIR.iconset"

mv "$DIR.icns" "output.icns"

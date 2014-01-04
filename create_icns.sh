#!/bin/sh

set -e

DIR=$(mktemp -d -t createicns)

cp logo-16-color.png "$DIR/icon_16x16.png"
cp logo-32.png "$DIR/icon_32x32.png"
cp logo-48.png "$DIR/icon_48x48.png"
cp logo-64.png "$DIR/icon_32x32@2x.png"
cp logo-128.png "$DIR/icon_128x128.png"

mv "$DIR" "$DIR.iconset"

iconutil -c icns "$DIR.iconset"

mv "$DIR.icns" "output.icns"

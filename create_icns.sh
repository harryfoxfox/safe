#!/bin/bash

set -e

DIR=$(mktemp -d -t createicns)

PATH_TO_INKSCAPE=inkscape

if ! which "$PATH_TO_INKSCAPE" > /dev/null && [ -d /Applications/Inkscape.app ]; then
    # guessing on mac
    PATH_TO_INKSCAPE="/Applications/Inkscape.app/Contents/Resources/bin/inkscape"
fi

function to_png () {
    "$PATH_TO_INKSCAPE" "$1" "--export-width=$2" "--export-height=$2" "--export-png=$3" --without-gui
}

if [ "$1" = to_png ]; then
    to_png "$2" "$3" "$4"
    exit 0
fi

if [ "$1" = menuBarIcon ]; then 
    to_png logo-16-bw.svg 16 "$DIR/icon_16x16.png"
    to_png logo-32-bw.svg 32 "$DIR/icon_16x16@2x.png"
else
    to_png logo-16-color.svg 16 "$DIR/icon_16x16.png"
    to_png logo-16-color.svg 32 "$DIR/icon_16x16@2x.png"
    to_png logo-32.svg 32 "$DIR/icon_32x32.png"
    to_png logo-128.svg 64 "$DIR/icon_32x32@2x.png"
    to_png logo-128.svg 128 "$DIR/icon_128x128.png"
    to_png logo-128.svg 256 "$DIR/icon_128x128@2x.png"
    to_png logo-128.svg 256 "$DIR/icon_256x256.png"
    to_png logo-128.svg 512 "$DIR/icon_256x256@2x.png"
    to_png logo-128.svg 512 "$DIR/icon_512x512.png"
    to_png logo-128.svg 1024 "$DIR/icon_512x512@2x.png"
fi

if which pngcrush > /dev/null; then
    cd "$DIR"
    for I in *.png; do
        pngcrush -ow -reduce -brute -l 9 $I;
    done;
    cd -
fi

mv "$DIR" "$DIR.iconset"

iconutil -c icns "$DIR.iconset"

mv "$DIR.icns" "output.icns"

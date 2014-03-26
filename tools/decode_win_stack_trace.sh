#!/bin/bash

ADDR2LINE="${BINUTILS_PREFIX}addr2line"
CPPFILT="${BINUTILS_PREFIX}c++filt"
OBJDUMP="${BINUTILS_PREFIX}objdump"

EXE="$1"
shift

# get ImageBase
IMAGE_BASE=0x$("$OBJDUMP" -p "$EXE" | awk '/ImageBase/ { print $2 }')

echo "Traceback (most recent call last):"

PRETTY=1

for ADDR in $@; do
    if [ $(($ADDR)) -ne 0 ]; then
        FN_NAME=$("$ADDR2LINE" -e "$EXE" -f 0x$(printf '%x' $(($IMAGE_BASE + $ADDR))) | awk '/^_/ { print "_"$0 } ! /^_/ { print $0 }' | head -n 1 | "$CPPFILT")
        TBLINE=$("$ADDR2LINE" -e "$EXE" -f 0x$(printf '%x' $(($IMAGE_BASE + $ADDR))) | awk '/^_/ { print "_"$0 } ! /^_/ { print $0 }' | tail -n 1 | head -n 1)
        if [ "x$PRETTY" != "x" ]; then
            FILENAME=$(echo "$TBLINE" | sed 's/^\([^:]\{1,\}\):[0-9]\{1,\}$/\1/')
            if [ "$FILENAME" = "??" ]; then
                echo "  <unknown>"
            else
                LINENO_=$(echo "$TBLINE" | sed 's/^[^:]\{1,\}:\([0-9]\{1,\}\)$/\1/')
                if [ -e "$FILENAME" ]; then
                    FILE_PATH="$FILENAME";
                else
                    FILENAME=$(basename "$FILENAME")
                    FILE_PATH=$(find . -name "$FILENAME" -type f | head -n 1 > /dev/null)
                fi

                echo "  File \"$FILE_PATH\", line $LINENO_, in $FN_NAME"
                CODE_LINE=$(cat "$FILE_PATH" | tail "+$LINENO_" 2>/dev/null | head -n 1 | sed 's/^[ '$'\t'']*\([^ '$'\t''].*\)$/    \1/')
                echo "$CODE_LINE"
            fi
        else
            echo "$FN_NAME $TBLINE"
        fi
    else
        echo "  <unknown>"
    fi
done


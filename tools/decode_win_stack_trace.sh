#!/bin/bash

ADDR2LINE="${BINUTILS_PREFIX}addr2line"
CPPFILT="${BINUTILS_PREFIX}c++filt"
OBJDUMP="${BINUTILS_PREFIX}objdump"

EXE="$1"
shift
REVISION="$1"
shift

# get ImageBase
IMAGE_BASE=0x$("$OBJDUMP" -p "$EXE" | awk '/ImageBase/ { print $2 }')

PRETTY=1

# disassemble file
DISASS=$(mktemp -t decode_win_stack_trace)a

"$OBJDUMP" -d "$EXE" > "$DISASS"

for ADDR in $@; do
    if [ $(($ADDR)) -ne 0 ]; then
        HEXADDR_RETURN=$(printf '%x' $(($IMAGE_BASE + $ADDR)))
        HEXADDR=0x$(cat "$DISASS" | fgrep -B1 "$HEXADDR_RETURN:" | head -n 1 | awk '{ print $1 }' | sed 's/:$//')
        FN_NAME=$("$ADDR2LINE" -e "$EXE" -f "$HEXADDR" | awk '/^_/ { print "_"$0 } ! /^_/ { print $0 }' | head -n 1 | "$CPPFILT")
        TBLINE=$("$ADDR2LINE" -e "$EXE" -f "$HEXADDR" | awk '/^_/ { print "_"$0 } ! /^_/ { print $0 }' | tail -n 1 | head -n 1)
        if [ "x$PRETTY" != "x" ]; then
            FILENAME=$(echo "$TBLINE" | sed 's/^\(.*\):[0-9]\{1,\}$/\1/')
            if [ "$FILENAME" = "??" ]; then
                echo "  <unknown> ($HEXADDR)"
            else
                LINENO_=$(echo "$TBLINE" | sed 's/^.*:\([0-9]\{1,\}\)$/\1/')
                BASE_FILENAME=$(echo "$FILENAME" | sed -E 's!^.*(/|\\)([^\\/]+)$!\2!')
                FILE_PATH=$(find . -name "$BASE_FILENAME" -type f | head -n 1)

                if [ ! -z "$FILE_PATH" ]; then
                    echo "  File \"$FILE_PATH\", line $LINENO_, in $FN_NAME ($HEXADDR)"
                    CODE_LINE=$(hg cat -r "$REVISION" "$FILE_PATH" | tail "+$LINENO_" 2>/dev/null | head -n 1 | sed 's/^[ '$'\t'']*\([^ '$'\t''].*\)$/    \1/')
                    echo "$CODE_LINE"
                else
                    echo "  File \"$FILENAME\", line $LINENO_, in $FN_NAME ($HEXADDR)"
                fi
            fi
        else
            echo "$FN_NAME $TBLINE"
        fi
    else
        echo "  <unknown>"
    fi
done


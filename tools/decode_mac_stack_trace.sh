#!/bin/bash

MAC_EXE="$1"
shift

echo "Traceback (most recent call last):"

PRETTY=1

for ADDR in $@; do
    if [ $(($ADDR)) -ne 0 ]; then
        TBLINE=$(xcrun atos -o "$MAC_EXE" -l 0x10000 $(printf "%x" $(($ADDR + 0x10000))) 2>/dev/null)
        if [ "x$PRETTY" != "x" ]; then
            FILENAME=$(echo "$TBLINE" | sed 's/^.*(\([^:]\{1,\}\):[0-9]\{1,\})$/\1/')
            if [ "$FILENAME" = "$TBLINE" ]; then
                echo "  <unformatted> $TBLINE"
            else
                LINENO_=$(echo "$TBLINE" | sed 's/^.*([^:]\{1,\}:\([0-9]\{1,\}\))$/\1/')
                FN_NAME=$(echo "$TBLINE" | sed 's/^\([^(]\{1,\}\).*$/\1/')
                FILE_PATH=$(find . -name "$FILENAME" -type f | head -n 1)
                echo "  File \"$FILE_PATH\", line $LINENO_, in $FN_NAME"
                CODE_LINE=$(cat "$FILE_PATH" | tail "+$LINENO_" 2>/dev/null | head -n 1 | sed 's/^ *\([^ ].*\)$/    \1/')
                echo "$CODE_LINE"
            fi
        else
            echo "$TBLINE"
        fi
    else
        echo "  <unknown>"
    fi
done


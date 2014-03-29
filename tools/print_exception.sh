#!/bin/sh

set -e

SCRIPT_DIR=$(dirname "$BASH_SOURCE[0]")
EXCEPTION_ID="$1"
DEBUG_INFO_DIR="$2"

EXCEPTION_FILE=$(mktemp -t safe_print_exception)

curl "http://dyn.www.getsafe.org/exceptions/${EXCEPTION_ID}" > "$EXCEPTION_FILE" 2>/dev/null

is_safe () {
    BAD_VAL=$(awk '! /^\s*([^=]+=(("[^"$]+")|([^$ \t]+)))?\s*$/' "$1")
    [ -z "$BAD_VAL" ]
}

if ! is_safe "$EXCEPTION_FILE" [; then
    echo "bad shell file from remote host"
    exit -1
fi

. "$EXCEPTION_FILE"

STACK_TRACE_ARG_=$(echo "$OFFSET_STACK_TRACE" | tr ',' ' ')

# reverse stack trace to get most recent call last
STACK_TRACE_ARG=""
for ADDR in $STACK_TRACE_ARG_; do
    STACK_TRACE_ARG="$ADDR $STACK_TRACE_ARG"
done

REVISION=VERSION-$VERSION

echo "Traceback (most recent call last)"

if [ "$TARGET_PLATFORM" = win ]; then
    "$SCRIPT_DIR/decode_win_stack_trace.sh" "$DEBUG_INFO_DIR/$VERSION/win/Safe-Debug.exe" $REVISION $STACK_TRACE_ARG
elif [ "$TARGET_PLATFORM" = mac ]; then
    "$SCRIPT_DIR/decode_mac_stack_trace.sh" "$DEBUG_INFO_DIR/$VERSION/mac/Safe" $REVISION $STACK_TRACE_ARG
else
    false
fi

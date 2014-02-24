#!/bin/sh

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
exec cmd //C $DIR/generate_signed_driver_security_catalog.bat "$1"

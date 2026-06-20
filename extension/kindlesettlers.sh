#!/bin/sh
EXT_DIR="$(dirname "$0")"
cd "$EXT_DIR" || exit 1
exec ./bin/start.sh

#!/bin/sh
EXT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$EXT_DIR" || exit 1
mkdir -p data
export LD_LIBRARY_PATH="$EXT_DIR/lib:$LD_LIBRARY_PATH"
exec "$EXT_DIR/bin/kindlesettlers" >> "$EXT_DIR/data/kindlesettlers.log" 2>&1

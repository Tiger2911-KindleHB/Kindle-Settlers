#!/bin/sh
EXT_DIR="/mnt/us/extensions/kindlesettlers"
if [ ! -x "$EXT_DIR/bin/start.sh" ]; then
  EXT_DIR="$(cd "$(dirname "$0")" && pwd)"
fi
exec "$EXT_DIR/bin/start.sh"

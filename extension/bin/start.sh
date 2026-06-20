#!/bin/sh
EXT_DIR="/mnt/us/extensions/kindlesettlers"
if [ ! -d "$EXT_DIR" ]; then
  EXT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
fi

cd "$EXT_DIR" || exit 1
mkdir -p "$EXT_DIR/data"
LOG="$EXT_DIR/data/kindlesettlers.log"

{
  echo "----- Kindle Settlers launch -----"
  date 2>/dev/null || true
  echo "EXT_DIR=$EXT_DIR"
  echo "DISPLAY(before)=${DISPLAY:-}"
} >> "$LOG" 2>&1

export DISPLAY="${DISPLAY:-:0.0}"
export HOME="${HOME:-/mnt/us}"
export LD_LIBRARY_PATH="$EXT_DIR/lib:${LD_LIBRARY_PATH:-}"

if [ ! -x "$EXT_DIR/bin/kindlesettlers" ]; then
  echo "ERROR: binary is missing or not executable: $EXT_DIR/bin/kindlesettlers" >> "$LOG"
  exit 126
fi

echo "Starting binary with DISPLAY=$DISPLAY" >> "$LOG"
exec "$EXT_DIR/bin/kindlesettlers" >> "$LOG" 2>&1

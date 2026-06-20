#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT_DIR/dist"
PKG_DIR="$OUT_DIR/kindlesettlers"

rm -rf "$OUT_DIR"
mkdir -p "$PKG_DIR/bin" "$PKG_DIR/assets/icons" "$PKG_DIR/assets/fonts" "$PKG_DIR/data"

cp -R "$ROOT_DIR/extension/"* "$PKG_DIR/"
cp "$ROOT_DIR/$BUILD_DIR/kindlesettlers" "$PKG_DIR/bin/kindlesettlers"
chmod +x "$PKG_DIR/kindlesettlers.sh" "$PKG_DIR/bin/start.sh" "$PKG_DIR/bin/kindlesettlers"

cd "$OUT_DIR"
zip -r kindlesettlers-kual.zip kindlesettlers

echo "$OUT_DIR/kindlesettlers-kual.zip"

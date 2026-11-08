#!/usr/bin/env bash
set -euo pipefail

ZIG="${ZIG:-zig}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

"$ZIG" cc -target aarch64-linux-musl -static -O2 -o uu_wheel_flip src/uu_wheel_flip.c

mkdir -p module dist
cp uu_wheel_flip module/uu_wheel_flip
chmod 755 module/uu_wheel_flip

rm -f dist/xiaomi-pad5-mouse-wheel-invert.zip
(
  cd module
  zip -r ../dist/xiaomi-pad5-mouse-wheel-invert.zip module.prop service.sh uu_wheel_flip
)

echo "built: $ROOT/dist/xiaomi-pad5-mouse-wheel-invert.zip"

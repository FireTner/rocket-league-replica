#!/bin/bash
# Offscreen capture: gamescope headless, wait for the PPM, kill the group, write PNG.
# Usage: capture.sh OUTDIR NAME [extra game args...]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:?outdir}"; NAME="${2:?name}"; shift 2
mkdir -p "$OUT"; rm -f "$OUT/$NAME.ppm" "$OUT/$NAME.png"
cd "$ROOT" || exit 1
setsid gamescope --backend headless -W 1280 -H 720 -r 60 -- ./build/game \
  --shell --no-vsync --no-audio --meshdir collision_meshes --seed 7 --ticks 160 \
  --no-shake --shot-at-tick 120 --shot-path "$OUT/$NAME.ppm" "$@" \
  >"$OUT/$NAME.log" 2>&1 &
p=$!
for _ in $(seq 1 200); do
  [ -s "$OUT/$NAME.ppm" ] && break
  sleep 0.15
done
sleep 0.5
kill -- "-$p" 2>/dev/null; wait "$p" 2>/dev/null
if [ -s "$OUT/$NAME.ppm" ]; then magick "$OUT/$NAME.ppm" "$OUT/$NAME.png" && echo "OK $NAME"; else echo "FAIL $NAME (no ppm)"; fi

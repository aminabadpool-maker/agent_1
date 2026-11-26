#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

target_ppm="${1:-}"
if [ -z "$target_ppm" ]; then
    target_ppm="$(ls -t renders/frame_*.ppm 2>/dev/null | head -n 1 || true)"
fi

if [ -z "$target_ppm" ] || [ ! -f "$target_ppm" ]; then
    echo "No PPM frame found. Render first with ./mini_blender" >&2
    exit 1
fi

if command -v feh >/dev/null 2>&1; then
    exec feh --scale-down "$target_ppm"
elif command -v xdg-open >/dev/null 2>&1; then
    exec xdg-open "$target_ppm"
elif command -v ffplay >/dev/null 2>&1; then
    exec ffplay -nodisp -autoexit "$target_ppm"
elif [ -x "./mini_blender" ]; then
    exec ./mini_blender -f 0 -n 1 -c 32 -o renders -p
else
    echo "No GUI viewer available and ./mini_blender not built." >&2
    exit 1
fi

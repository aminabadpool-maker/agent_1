#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -x "./mini_blender" ]; then
    make
fi

open_laptop_gui() {
    local latest
    latest="$(ls -t renders/frame_*.ppm 2>/dev/null | head -n 1 || true)"
    if [ -z "$latest" ]; then
        echo "No rendered PPM frame found in renders/."
        return
    fi

    if command -v feh >/dev/null 2>&1; then
        nohup feh --scale-down "$latest" >/dev/null 2>&1 &
        echo "Opened in feh: $latest"
    elif command -v xdg-open >/dev/null 2>&1; then
        nohup xdg-open "$latest" >/dev/null 2>&1 &
        echo "Opened with xdg-open: $latest"
    elif command -v ffplay >/dev/null 2>&1; then
        nohup ffplay -nodisp -autoexit "$latest" >/dev/null 2>&1 &
        echo "Played via ffplay: $latest"
    else
        ./mini_blender -f 0 -n 1 -c 32 -o renders -p
    fi
}

if [ "${1:-}" = "--gui" ]; then
    open_laptop_gui
    exit 0
fi

exec ./mini_blender -i

#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p renders logs scenes

out_dir="renders/long_job"
frames=120
cycle=120
scene="scenes/kungfu_temple_blocking.xml"
max_retries=3

while [ "$#" -gt 0 ]; do
    case "$1" in
        -o) out_dir="$2"; shift 2 ;;
        -n) frames="$2"; shift 2 ;;
        -c) cycle="$2"; shift 2 ;;
        -s) scene="$2"; shift 2 ;;
        -r) max_retries="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "$out_dir" logs

# Estimate ~950 KB per PPM frame + 64 MB safety reserve
need_kb=$((frames * 960 + 65536))
avail_kb=$(df -P "$out_dir" | awk 'NR==2 {print $4}')
echo "Disk check for '${out_dir}': need ~${need_kb} KB, available ${avail_kb} KB"
if [ "${avail_kb:-0}" -lt "$need_kb" ]; then
    echo "Error: insufficient disk space for ${frames} frames." >&2
    exit 1
fi

if [ ! -x "./mini_blender_pi" ]; then
    make pi
fi

attempt=1
while [ "$attempt" -le "$max_retries" ]; do
    echo "[Attempt ${attempt}/${max_retries}] Running ${frames} frames into '${out_dir}' with --resume..."
    if ./mini_blender_pi -f 0 -n "$frames" -c "$cycle" -o "$out_dir" -s "$scene" --resume -q; then
        echo "Long run completed safely."
        exit 0
    fi
    echo "Warning: attempt ${attempt} interrupted, retrying..."
    attempt=$((attempt + 1))
done

echo "Error: failed after ${max_retries} attempts." >&2
exit 1

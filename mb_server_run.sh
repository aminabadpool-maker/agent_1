#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -x "./mini_blender_pi" ]; then
    make pi
fi

frames="${1:-60}"
scene="${2:-scenes/heavy_stress_scene.xml}"
run_id="${3:-run_$$}"
out_dir="renders/${run_id}"

# Guard 1: bound single-batch frame count to protect shared server CPU/disk
if ! [[ "$frames" =~ ^[0-9]+$ ]] || [ "$frames" -lt 1 ]; then
    echo "Error: invalid frame count '$frames'" >&2
    exit 1
fi
if [ "$frames" -gt 1000 ]; then
    echo "Error: single-batch frame count capped at 1000 for server safety (got $frames)" >&2
    exit 1
fi

# Guard 2: require at least 250MB free space before writing raw PPM frames
mkdir -p "$out_dir" logs
avail_kb="$(df -Pk "$out_dir" | awk 'NR==2 {print $4}')"
if [ -n "$avail_kb" ] && [ "$avail_kb" -lt 256000 ]; then
    echo "Error: low disk space (${avail_kb} KB available) for $out_dir" >&2
    exit 1
fi

log_file="logs/${run_id}.log"
{
    echo "=== SERVER GUARD RUN: ${run_id} | $(date) ==="
    echo "Frames: ${frames} | Scene: ${scene} | Out: ${out_dir}"
    ./mini_blender_pi -f 0 -n "$frames" -c 60 -o "$out_dir" -s "$scene" -q
    echo "Files created: $(find "$out_dir" -maxdepth 1 -name 'frame_*.ppm' | wc -l)"
    echo "Output size: $(du -sh "$out_dir" | awk '{print $1}')"
    echo "=== END ${run_id} ==="
} | tee "$log_file"

#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p renders logs scenes

frames="${1:-20}"
scene="${2:-scenes/kungfu_temple_blocking.xml}"
job_tag="${3:-server_job}"

if ! [[ "$frames" =~ ^[0-9]+$ ]] || [ "$frames" -lt 1 ] || [ "$frames" -gt 100000 ]; then
    echo "Error: invalid frame count '$frames'" >&2
    exit 1
fi

safe_tag=$(printf "%s" "$job_tag" | tr -c 'A-Za-z0-9_-' '_')
out_dir="renders/${safe_tag}"
lock_dir="logs/.lock_${safe_tag}"
log_file="logs/${safe_tag}.log"

mkdir -p "$out_dir" logs

if ! mkdir "$lock_dir" 2>/dev/null; then
    echo "Error: job '${safe_tag}' is already running (lock exists: ${lock_dir})" >&2
    exit 1
fi
trap 'rmdir "$lock_dir" 2>/dev/null || true' EXIT

if [ ! -x "./mini_blender_pi" ]; then
    if [ -f "Makefile" ] && grep -q '^pi:' Makefile; then
        make pi
    else
        cc -std=c11 -O3 -Wall -Wextra -Wpedantic -DMB_NO_PREVIEW src/mini_blender.c -o mini_blender_pi -lm
    fi
fi

need_kb=$((frames * 960 + 65536))
avail_kb=$(df -P "$out_dir" | awk 'NR==2 {print $4}')
if [ "${avail_kb:-0}" -lt "$need_kb" ]; then
    echo "Error: insufficient disk space for job '${safe_tag}'" >&2
    exit 1
fi

{
    echo "=== SERVER JOB START: ${safe_tag} | $(date) ==="
    ./mini_blender_pi -f 0 -n "$frames" -c 60 -o "$out_dir" -s "$scene" --resume -q
    echo "=== SERVER JOB END: ${safe_tag} | $(date) ==="
} | tee "$log_file"

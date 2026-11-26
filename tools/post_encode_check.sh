#!/usr/bin/env bash
set -euo pipefail

mp4_path="${1:-renders/pi_run/out.mp4}"
if [ ! -s "$mp4_path" ]; then
    echo "Error: MP4 missing or empty: $mp4_path" >&2
    exit 1
fi
sha256sum "$mp4_path" > "${mp4_path}.sha256"
echo "[PKG post-encode] Verified & checksummed: ${mp4_path}.sha256"

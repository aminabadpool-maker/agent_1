#!/usr/bin/env bash
set -euo pipefail

mp4_path="${1:-renders/srv_run/out.mp4}"
merged_dir="${2:-renders/srv_run/merged}"
summary_path="${3:-$(dirname "$mp4_path")/run_summary.txt}"

if [ ! -s "$mp4_path" ]; then
    echo "Error: MP4 missing or empty for summary: '${mp4_path}'" >&2
    exit 1
fi

sha_path="${mp4_path}.sha256"
sha_status="missing"
if [ -f "$sha_path" ]; then
    sha_status="present"
fi

manifest_path="${merged_dir}/ppm_manifest.tsv"
audited_frames=0
if [ -f "$manifest_path" ]; then
    audited_frames="$(awk 'NR>1 {c++} END {print c+0}' "$manifest_path")"
fi

mp4_bytes="$(wc -c < "$mp4_path")"
timestamp="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

{
    printf "timestamp_utc=%s\n" "$timestamp"
    printf "mp4_file=%s\n" "$mp4_path"
    printf "mp4_bytes=%s\n" "$mp4_bytes"
    printf "sha256_file=%s\n" "$sha_status"
    printf "audited_ppm_frames=%s\n" "$audited_frames"
} > "$summary_path"

echo "[PKG server-summary] Wrote ${summary_path} (frames=${audited_frames}, mp4_bytes=${mp4_bytes}, sha256=${sha_status})"

#!/usr/bin/env bash
set -euo pipefail

profile="${1:-server}"
frames="${2:-10}"
out_base="${3:-renders/srv_run}"
merged_dir="${out_base}/merged"
mp4_path="${out_base}/out.mp4"
log_file="${out_base}/benchmark.tsv"

./tools/preflight_check.sh "$out_base" "$profile"

start_ns="$(date +%s%N 2>/dev/null || true)"
start_s="$(date +%s)"

./pipeline_run.sh --profile "$profile" --run -t "$frames"

end_ns="$(date +%s%N 2>/dev/null || true)"
end_s="$(date +%s)"

if [ -n "$start_ns" ] && [ -n "$end_ns" ] && [ "$start_ns" != "$end_ns" ] && [[ "$start_ns" =~ ^[0-9]+$ ]] && [[ "$end_ns" =~ ^[0-9]+$ ]]; then
    elapsed_sec="$(awk "BEGIN { printf \"%.3f\", ($end_ns - $start_ns) / 1000000000 }")"
else
    elapsed_sec="$((end_s - start_s)).000"
fi

audited_frames=0
if [ -f "${merged_dir}/ppm_manifest.tsv" ]; then
    audited_frames="$(awk 'NR > 1 { c++ } END { print c + 0 }' "${merged_dir}/ppm_manifest.tsv")"
fi

mp4_bytes=0
if [ -f "$mp4_path" ]; then
    mp4_bytes="$(wc -c < "$mp4_path")"
fi

ms_per_frame="$(awk -v el="$elapsed_sec" -v fr="$audited_frames" 'BEGIN { if (fr > 0) printf "%.2f", (el * 1000) / fr; else printf "0.00" }')"

if [ ! -f "$log_file" ]; then
    printf "timestamp_utc\tprofile\trequested_frames\taudited_frames\telapsed_sec\tms_per_frame\tmp4_bytes\n" > "$log_file"
fi

ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$ts" "$profile" "$frames" "$audited_frames" "$elapsed_sec" "$ms_per_frame" "$mp4_bytes" >> "$log_file"

cat "$log_file"

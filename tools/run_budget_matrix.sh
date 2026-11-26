#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./tools/pi_budget}"
FRAMES="${2:-300}"
CORES="${3:-4}"

if [ ! -x "$BIN" ]; then
    printf "[ERR] Binary not executable: %s\n" "$BIN" >&2
    exit 1
fi

RESOLUTIONS=("640 480" "1280 720" "1920 1080")
RAMS=(256 1001 4096)

printf "%-11s | %-10s | %-8s | %-7s | %-12s | %-14s | %-14s\n" \
    "RESOLUTION" "AVAIL_MB" "FEASIBLE" "THREADS" "BATCH_FRAMES" "PPM_BYTES" "BUDGET_BYTES"
printf -- "------------+------------+----------+---------+--------------+----------------+----------------\n"

for res in "${RESOLUTIONS[@]}"; do
    read -r w h <<< "$res"
    for ram in "${RAMS[@]}"; do
        # نیمی از رم در دسترس به عنوان بودجه پایه در سناریوهای تست (حداقل 32MB)
        budget=$(( ram / 2 ))
        if [ "$budget" -lt 32 ]; then
            budget=32
        fi

        set +e
        out="$("$BIN" "$w" "$h" "$FRAMES" "$ram" "$CORES" "$budget" 2>/dev/null)"
        rc=$?
        set -e

        feasible="$(printf '%s\n' "$out" | awk '{for(i=1;i<=NF;i++) if($i ~ /^feasible=/) {sub(/^feasible=/,"",$i); print $i}}')"
        threads="$(printf '%s\n' "$out" | awk '{for(i=1;i<=NF;i++) if($i ~ /^threads=/) {sub(/^threads=/,"",$i); print $i}}')"
        batch="$(printf '%s\n' "$out" | awk '{for(i=1;i<=NF;i++) if($i ~ /^batch_frames=/) {sub(/^batch_frames=/,"",$i); print $i}}')"
        ppm="$(printf '%s\n' "$out" | awk '{for(i=1;i<=NF;i++) if($i ~ /^ppm_bytes=/) {sub(/^ppm_bytes=/,"",$i); print $i}}')"
        bbytes="$(printf '%s\n' "$out" | awk '{for(i=1;i<=NF;i++) if($i ~ /^budget_bytes=/) {sub(/^budget_bytes=/,"",$i); print $i}}')"

        printf "%-11s | %-10s | %-8s | %-7s | %-12s | %-14s | %-14s (rc=%d)\n" \
            "${w}x${h}" "${ram}MB" "${feasible:-0}" "${threads:-0}" "${batch:-0}" "${ppm:-0}" "${bbytes:-0}" "$rc"
    done
done

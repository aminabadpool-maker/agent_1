#!/usr/bin/env bash
set -euo pipefail

merged_dir="${1:-${MERGED_DIR:-}}"
exp_w="${2:-${MB_WIDTH:-}}"
exp_h="${3:-${MB_HEIGHT:-}}"

if [ -z "$merged_dir" ] || [ ! -d "$merged_dir" ]; then
    echo "Error: MERGED_DIR not found: '${merged_dir}'" >&2
    exit 1
fi

shopt -s nullglob
frames=("$merged_dir"/frame_*.ppm)
shopt -u nullglob

count="${#frames[@]}"
if [ "$count" -eq 0 ]; then
    echo "Error: no frame_*.ppm files found in '${merged_dir}'" >&2
    exit 1
fi

manifest_path="${merged_dir}/ppm_manifest.tsv"
: > "$manifest_path"
printf "index\tfile\tbytes\n" >> "$manifest_path"

total_bytes=0
idx=0

for f in "${frames[@]}"; do
    if [ ! -s "$f" ]; then
        echo "Error: empty PPM file '${f}'" >&2
        exit 1
    fi

    magic="$(head -n 1 "$f" | tr -d '\r')"
    if [ "$magic" != "P6" ]; then
        echo "Error: invalid magic '${magic}' in '${f}'" >&2
        exit 1
    fi

    dims_line="$(awk 'NR==1{next} /^#/{next} {print; exit}' "$f" | tr -d '\r')"
    w="$(printf '%s\n' "$dims_line" | awk '{print $1}')"
    h="$(printf '%s\n' "$dims_line" | awk '{print $2}')"

    if [ -n "$exp_w" ] && [ "$w" != "$exp_w" ]; then
        echo "Error: width mismatch in '${f}': got ${w}, expected ${exp_w}" >&2
        exit 1
    fi
    if [ -n "$exp_h" ] && [ "$h" != "$exp_h" ]; then
        echo "Error: height mismatch in '${f}': got ${h}, expected ${exp_h}" >&2
        exit 1
    fi

    sz="$(wc -c < "$f")"
    total_bytes=$((total_bytes + sz))
    printf "%d\t%s\t%s\n" "$idx" "$(basename "$f")" "$sz" >> "$manifest_path"
    idx=$((idx + 1))
done

echo "[PKG post-collect] Audited ${count} frames (${exp_w:-?}x${exp_h:-?}), total=${total_bytes}B -> ${manifest_path}"

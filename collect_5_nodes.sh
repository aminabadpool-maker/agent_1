#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

out_base="renders"
merged_dir="renders/merged_frames"
move_files=0
expected_min_bytes=$((640 * 480 * 3))
expected_max_bytes=$((640 * 480 * 3 + 128))

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b) out_base="$2"; shift 2 ;;
        -o) merged_dir="$2"; shift 2 ;;
        --move) move_files=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "$merged_dir"

total_ok=0
total_bad=0
prev_idx=-1
gaps_found=0

echo "=== COLLECTING & VERIFYING FRAMES FROM ${out_base}/node_01..node_05 -> ${merged_dir} ==="

for idx in 1 2 3 4 5; do
    node_tag=$(printf "node_%02d" "$idx")
    node_dir="${out_base}/${node_tag}"

    if [ ! -d "$node_dir" ]; then
        echo "[${node_tag}] WARNING: directory '${node_dir}' not found, skipping."
        continue
    fi

    node_ok=0
    node_bad=0

    while IFS= read -r -d '' src_file; do
        base_name=$(basename "$src_file")
        if ! [[ "$base_name" =~ ^frame_([0-9]+)\.ppm$ ]]; then
            continue
        fi
        frame_num=$((10#${BASH_REMATCH[1]}))

        if [ ! -s "$src_file" ]; then
            echo "  [BAD] ${src_file}: empty file (0 bytes)"
            node_bad=$((node_bad + 1))
            continue
        fi

        f_size=$(wc -c < "$src_file")
        if [ "$f_size" -lt "$expected_min_bytes" ] || [ "$f_size" -gt "$expected_max_bytes" ]; then
            echo "  [BAD] ${src_file}: unexpected size ${f_size} bytes (expected ~${expected_min_bytes} + header)"
            node_bad=$((node_bad + 1))
            continue
        fi

        if [ "$prev_idx" -ge 0 ] && [ "$frame_num" -ne $((prev_idx + 1)) ]; then
            echo "  [GAP] Frame jump detected: after frame_${prev_idx}, saw frame_${frame_num}"
            gaps_found=$((gaps_found + 1))
        fi
        prev_idx="$frame_num"

        dst_file="${merged_dir}/${base_name}"
        if [ "$move_files" -eq 1 ]; then
            mv -f "$src_file" "$dst_file"
        else
            cp -f "$src_file" "$dst_file"
        fi
        node_ok=$((node_ok + 1))
    done < <(find "$node_dir" -maxdepth 1 -name 'frame_*.ppm' -print0 | sort -z)

    echo "[${node_tag}] valid=${node_ok} | invalid=${node_bad}"
    total_ok=$((total_ok + node_ok))
    total_bad=$((total_bad + node_bad))
done

merged_count=$(find "$merged_dir" -maxdepth 1 -name 'frame_*.ppm' | wc -l)
merged_size=$(du -sh "$merged_dir" | awk '{print $1}')

echo "=== COLLECTION SUMMARY ==="
echo "Valid frames collected: ${total_ok} | Invalid frames skipped: ${total_bad} | Sequence gaps: ${gaps_found}"
echo "Unified folder: ${merged_dir} | Total PPM files: ${merged_count} | Size: ${merged_size}"

#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

out_base="renders"
merged_dir="renders/merged_frames"
move_files=0
req_w=""
req_h=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b) out_base="$2"; shift 2 ;;
        -o) merged_dir="$2"; shift 2 ;;
        -w) req_w="$2"; shift 2 ;;
        -h) req_h="$2"; shift 2 ;;
        --move) move_files=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if { [ -n "$req_w" ] && [ -z "$req_h" ]; } || { [ -z "$req_w" ] && [ -n "$req_h" ]; }; then
    echo "Error: both -w and -h must be provided together." >&2
    exit 1
fi

if [ -n "$req_w" ]; then
    if ! [[ "$req_w" =~ ^[0-9]+$ ]] || ! [[ "$req_h" =~ ^[0-9]+$ ]] || [ "$req_w" -lt 16 ] || [ "$req_h" -lt 16 ]; then
        echo "Error: invalid -w/-h dimensions." >&2
        exit 1
    fi
fi

read_ppm_dims() {
    local file="$1"
    python3 - "$file" <<'PY'
import sys

path = sys.argv[1]
try:
    with open(path, "rb") as f:
        header_tokens = []
        while len(header_tokens) < 4:
            line = f.readline()
            if not line:
                break
            line_str = line.decode("ascii", errors="ignore")
            for part in line_str.split():
                if part.startswith("#"):
                    break
                header_tokens.append(part)
                if len(header_tokens) == 4:
                    break
        if len(header_tokens) < 4 or header_tokens[0] != "P6":
            sys.exit(1)
        w = int(header_tokens[1])
        h = int(header_tokens[2])
        maxval = int(header_tokens[3])
        if w <= 0 or h <= 0 or maxval <= 0 or maxval > 65535:
            sys.exit(1)
        print(f"{w} {h}")
except Exception:
    sys.exit(1)
PY
}

mkdir -p "$merged_dir"

total_ok=0
total_bad=0
prev_idx=-1
gaps_found=0
ref_dims=""

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

        dims=$(read_ppm_dims "$src_file" || true)
        if [ -z "$dims" ]; then
            echo "  [BAD] ${src_file}: invalid or unreadable P6 PPM header"
            node_bad=$((node_bad + 1))
            continue
        fi

        img_w="${dims%% *}"
        img_h="${dims##* }"

        if [ -n "$req_w" ] && { [ "$img_w" -ne "$req_w" ] || [ "$img_h" -ne "$req_h" ]; }; then
            echo "  [BAD] ${src_file}: dimensions ${img_w}x${img_h} do not match expected ${req_w}x${req_h}"
            node_bad=$((node_bad + 1))
            continue
        fi

        if [ -z "$ref_dims" ]; then
            ref_dims="${img_w}x${img_h}"
        elif [ "${img_w}x${img_h}" != "$ref_dims" ]; then
            echo "  [RES-CHANGE] ${src_file}: dimensions changed from ${ref_dims} to ${img_w}x${img_h}"
            ref_dims="${img_w}x${img_h}"
        fi

        expected_min_bytes=$((img_w * img_h * 3))
        expected_max_bytes=$((expected_min_bytes + 256))
        f_size=$(wc -c < "$src_file")

        if [ "$f_size" -lt "$expected_min_bytes" ] || [ "$f_size" -gt "$expected_max_bytes" ]; then
            echo "  [BAD] ${src_file}: unexpected size ${f_size} bytes for ${img_w}x${img_h} (expected ~${expected_min_bytes} + header)"
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

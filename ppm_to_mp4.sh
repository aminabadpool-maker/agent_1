#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

in_dir="renders"
out_file=""
fps="24"
start_num=""
crf="20"
clean_ppm="0"

while [ "$#" -gt 0 ]; do
    case "$1" in
        -i|--input-dir) in_dir="$2"; shift 2 ;;
        -o|--output) out_file="$2"; shift 2 ;;
        -f|--fps) fps="$2"; shift 2 ;;
        -s|--start) start_num="$2"; shift 2 ;;
        --crf) crf="$2"; shift 2 ;;
        --clean-ppm) clean_ppm="1"; shift 1 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Error: ffmpeg is not installed or not in PATH." >&2
    exit 1
fi

if [ ! -d "$in_dir" ]; then
    echo "Error: input directory '$in_dir' not found." >&2
    exit 1
fi

first_ppm=$(find "$in_dir" -maxdepth 1 -name 'frame_*.ppm' | sort | head -n 1 || true)
if [ -z "$first_ppm" ]; then
    echo "Error: no frame_*.ppm files found in '$in_dir'." >&2
    exit 1
fi

if [ -z "$start_num" ]; then
    base_name=$(basename "$first_ppm")
    num_part=${base_name#frame_}
    num_part=${num_part%.ppm}
    start_num=$((10#$num_part))
fi

if [ -z "$out_file" ]; then
    dir_base=$(basename "$in_dir")
    out_file="${in_dir}/${dir_base}.mp4"
fi

mkdir -p "$(dirname "$out_file")"

echo "Input dir : $in_dir"
echo "Start num : $start_num"
echo "FPS       : $fps"
echo "CRF       : $crf"
echo "Output MP4: $out_file"

ffmpeg -y \
    -framerate "$fps" \
    -start_number "$start_num" \
    -i "${in_dir}/frame_%04d.ppm" \
    -c:v libx264 \
    -pix_fmt yuv420p \
    -crf "$crf" \
    "$out_file"

echo "Done -> $out_file"
if [ "$clean_ppm" = "1" ] && [ -s "$out_file" ]; then
    rm -f "${in_dir}"/frame_*.ppm
    echo "Cleaned intermediate PPM frames in $in_dir"
fi

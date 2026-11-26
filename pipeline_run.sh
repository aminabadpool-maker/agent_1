#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

profile=""
mode="plan"
start_frame=0
total_frames=250
cycle=60
scene="scenes/kungfu_temple_blocking.xml"

width="${MB_WIDTH:-640}"
height="${MB_HEIGHT:-480}"
fps="${FPS:-24}"
crf="${CRF:-20}"
out_base="${OUT_BASE:-renders/job}"
merged_dir="${MERGED_DIR:-}"
out_mp4="${OUT_MP4:-}"
move_files="${MOVE_FILES:-1}"
clean_ppm="${CLEAN_PPM:-1}"
hook_post_collect="${HOOK_POST_COLLECT:-}"
hook_post_encode="${HOOK_POST_ENCODE:-}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile) profile="$2"; shift 2 ;;
        --plan) mode="plan"; shift ;;
        --run) mode="run"; shift ;;
        -f) start_frame="$2"; shift 2 ;;
        -t) total_frames="$2"; shift 2 ;;
        -c) cycle="$2"; shift 2 ;;
        -W) width="$2"; shift 2 ;;
        -H) height="$2"; shift 2 ;;
        -s) scene="$2"; shift 2 ;;
        -o) out_base="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -n "$profile" ]; then
    env_file="profiles/${profile}.env"
    if [ ! -f "$env_file" ]; then
        echo "Error: profile '$env_file' not found." >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    source "$env_file"
fi

# Re-apply explicit CLI overrides if provided after --profile
args_copy=("$@")
_idx=0
while [ "$_idx" -lt "${#args_copy[@]}" ]; do
    tok="${args_copy[$_idx]}"
    nxt_idx=$((_idx + 1))
    case "$tok" in
        -f) start_frame="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        -t) total_frames="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        -c) cycle="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        -W) width="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        -H) height="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        -s) scene="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        -o) out_base="${args_copy[$nxt_idx]}"; _idx=$((_idx + 2)) ;;
        *) _idx=$((_idx + 1)) ;;
    esac
done

width="${MB_WIDTH:-$width}"
height="${MB_HEIGHT:-$height}"
fps="${FPS:-$fps}"
crf="${CRF:-$crf}"
out_base="${OUT_BASE:-$out_base}"
merged_dir="${MERGED_DIR:-${out_base}/merged}"
out_mp4="${OUT_MP4:-${out_base}/out.mp4}"
move_files="${MOVE_FILES:-$move_files}"
clean_ppm="${CLEAN_PPM:-$clean_ppm}"
hook_post_collect="${HOOK_POST_COLLECT:-$hook_post_collect}"
hook_post_encode="${HOOK_POST_ENCODE:-$hook_post_encode}"

export MB_WIDTH="$width"
export MB_HEIGHT="$height"

split_cmd="./split_5_nodes.sh --${mode} -f ${start_frame} -t ${total_frames} -c ${cycle} -W ${width} -H ${height} -s ${scene} -o ${out_base}"
collect_cmd="./collect_5_nodes.sh -b ${out_base} -o ${merged_dir} -w ${width} -h ${height}"
if [ "$move_files" -eq 1 ]; then
    collect_cmd="${collect_cmd} --move"
fi

encode_cmd="./ppm_to_mp4.sh -i ${merged_dir} -o ${out_mp4} -f ${fps} -s ${start_frame} --crf ${crf}"
if [ "$clean_ppm" -eq 1 ]; then
    encode_cmd="${encode_cmd} --clean-ppm"
fi

echo "=== PIPELINE (${mode}) | res=${width}x${height} | base=${out_base} ==="
echo "[1/3 SPLIT]   ${split_cmd}"
echo "[2/3 COLLECT] ${collect_cmd}"
echo "[3/3 ENCODE]  ${encode_cmd}"

if [ "$mode" = "plan" ]; then
    eval "$split_cmd"
    exit 0
fi

mkdir -p "$out_base" "$merged_dir" "$(dirname "$out_mp4")"

eval "$split_cmd"
eval "$collect_cmd"

if [ -n "$hook_post_collect" ]; then
    echo "[HOOK post-collect] ${hook_post_collect}"
    eval "$hook_post_collect"
fi

eval "$encode_cmd"

if [ -n "$hook_post_encode" ]; then
    echo "[HOOK post-encode] ${hook_post_encode}"
    eval "$hook_post_encode"
fi

echo "=== PIPELINE DONE -> ${out_mp4} ==="

#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

profile=""
mode="plan"
start_frame=0
total_frames=250
cycle=60
scene="scenes/kungfu_temple_blocking.xml"

env_w="${MB_WIDTH:-}"
env_h="${MB_HEIGHT:-}"
env_fps="${FPS:-}"
env_crf="${CRF:-}"
env_out_base="${OUT_BASE:-}"
env_merged_dir="${MERGED_DIR:-}"
env_out_mp4="${OUT_MP4:-}"
env_move_files="${MOVE_FILES:-}"
env_clean_ppm="${CLEAN_PPM:-}"
env_hook_post_collect="${HOOK_POST_COLLECT:-}"
env_hook_post_encode="${HOOK_POST_ENCODE:-}"

prof_w=""
prof_h=""
prof_fps=""
prof_crf=""
prof_out_base=""
prof_merged_dir=""
prof_out_mp4=""
prof_move_files=""
prof_clean_ppm=""
prof_hook_post_collect=""
prof_hook_post_encode=""

cli_start_frame=""
cli_total_frames=""
cli_cycle=""
cli_w=""
cli_h=""
cli_scene=""
cli_out_base=""

raw_args=("$@")
i=0
while [ "$i" -lt "${#raw_args[@]}" ]; do
    tok="${raw_args[$i]}"
    case "$tok" in
        --profile)
            profile="${raw_args[$((i + 1))]}"
            i=$((i + 2))
            ;;
        *)
            i=$((i + 1))
            ;;
    esac
done

if [ -n "$profile" ]; then
    env_file="profiles/${profile}.env"
    if [ ! -f "$env_file" ]; then
        echo "Error: profile '$env_file' not found." >&2
        exit 1
    fi
    # Load profile variables in an isolated subshell/namespace so they never clobber CLI flags
    MB_WIDTH=""
    MB_HEIGHT=""
    FPS=""
    CRF=""
    OUT_BASE=""
    MERGED_DIR=""
    OUT_MP4=""
    MOVE_FILES=""
    CLEAN_PPM=""
    HOOK_POST_COLLECT=""
    HOOK_POST_ENCODE=""
    # shellcheck disable=SC1090
    source "$env_file"
    prof_w="${MB_WIDTH:-}"
    prof_h="${MB_HEIGHT:-}"
    prof_fps="${FPS:-}"
    prof_crf="${CRF:-}"
    prof_out_base="${OUT_BASE:-}"
    prof_merged_dir="${MERGED_DIR:-}"
    prof_out_mp4="${OUT_MP4:-}"
    prof_move_files="${MOVE_FILES:-}"
    prof_clean_ppm="${CLEAN_PPM:-}"
    prof_hook_post_collect="${HOOK_POST_COLLECT:-}"
    prof_hook_post_encode="${HOOK_POST_ENCODE:-}"
fi

i=0
while [ "$i" -lt "${#raw_args[@]}" ]; do
    tok="${raw_args[$i]}"
    case "$tok" in
        --profile) i=$((i + 2)) ;;
        --plan) mode="plan"; i=$((i + 1)) ;;
        --run) mode="run"; i=$((i + 1)) ;;
        -f) cli_start_frame="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        -t) cli_total_frames="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        -c) cli_cycle="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        -W) cli_w="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        -H) cli_h="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        -s) cli_scene="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        -o) cli_out_base="${raw_args[$((i + 1))]}"; i=$((i + 2)) ;;
        *) echo "Unknown option: $tok" >&2; exit 1 ;;
    esac
done

start_frame="${cli_start_frame:-$start_frame}"
total_frames="${cli_total_frames:-$total_frames}"
cycle="${cli_cycle:-$cycle}"
scene="${cli_scene:-$scene}"

width="${cli_w:-${prof_w:-${env_w:-640}}}"
height="${cli_h:-${prof_h:-${env_h:-480}}}"
fps="${prof_fps:-${env_fps:-24}}"
crf="${prof_crf:-${env_crf:-20}}"
out_base="${cli_out_base:-${prof_out_base:-${env_out_base:-renders/job}}}"
merged_dir="${prof_merged_dir:-${env_merged_dir:-${out_base}/merged}}"
out_mp4="${prof_out_mp4:-${env_out_mp4:-${out_base}/out.mp4}}"
move_files="${prof_move_files:-${env_move_files:-1}}"
clean_ppm="${prof_clean_ppm:-${env_clean_ppm:-1}}"
hook_post_collect="${prof_hook_post_collect:-${env_hook_post_collect:-}}"
hook_post_encode="${prof_hook_post_encode:-${env_hook_post_encode:-}}"

export MB_WIDTH="$width"
export MB_HEIGHT="$height"

split_args=(
    ./split_5_nodes.sh "--${mode}"
    -f "$start_frame"
    -t "$total_frames"
    -c "$cycle"
    -W "$width"
    -H "$height"
    -s "$scene"
    -o "$out_base"
)

collect_args=(
    ./collect_5_nodes.sh
    -b "$out_base"
    -o "$merged_dir"
    -w "$width"
    -h "$height"
)
if [ "$move_files" -eq 1 ]; then
    collect_args+=(--move)
fi

encode_args=(
    ./ppm_to_mp4.sh
    -i "$merged_dir"
    -o "$out_mp4"
    -f "$fps"
    -s "$start_frame"
    --crf "$crf"
)
if [ "$clean_ppm" -eq 1 ]; then
    encode_args+=(--clean-ppm)
fi

print_cmd() {
    printf '%q ' "$@"
    printf '\n'
}

echo "=== PIPELINE (${mode}) | res=${width}x${height} | base=${out_base} ==="
printf "[1/3 SPLIT]   "; print_cmd "${split_args[@]}"
printf "[2/3 COLLECT] "; print_cmd "${collect_args[@]}"
printf "[3/3 ENCODE]  "; print_cmd "${encode_args[@]}"

if [ "$mode" = "plan" ]; then
    "${split_args[@]}"
    exit 0
fi

mkdir -p "$out_base" "$merged_dir" "$(dirname "$out_mp4")"

"${split_args[@]}"
"${collect_args[@]}"

if [ -n "$hook_post_collect" ]; then
    echo "[HOOK post-collect] ${hook_post_collect}"
    bash -lc "$hook_post_collect"
fi

"${encode_args[@]}"

if [ -n "$hook_post_encode" ]; then
    echo "[HOOK post-encode] ${hook_post_encode}"
    bash -lc "$hook_post_encode"
fi

echo "=== PIPELINE DONE -> ${out_mp4} ==="

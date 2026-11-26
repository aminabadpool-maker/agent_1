#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p scenes renders logs

mode="plan"
start_frame=0
total_frames=250
cycle=60
width="${MB_WIDTH:-640}"
height="${MB_HEIGHT:-480}"
scene="scenes/kungfu_temple_blocking.xml"
out_base="renders"
extra_args="-q"

while [ "$#" -gt 0 ]; do
    case "$1" in
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

if ! [[ "$start_frame" =~ ^[0-9]+$ ]] || ! [[ "$total_frames" =~ ^[0-9]+$ ]] || [ "$total_frames" -lt 5 ]; then
    echo "Error: start_frame must be >= 0 and total_frames must be >= 5" >&2
    exit 1
fi

if ! [[ "$width" =~ ^[0-9]+$ ]] || ! [[ "$height" =~ ^[0-9]+$ ]] || [ "$width" -lt 16 ] || [ "$height" -lt 16 ]; then
    echo "Error: width (-W) and height (-H) must be integers >= 16" >&2
    exit 1
fi

if [ "$mode" = "run" ] && [ ! -x "./mini_blender_pi" ]; then
    if [ -f "Makefile" ] && grep -q '^pi:' Makefile; then
        make pi
    else
        cc -std=c11 -O3 -Wall -Wextra -Wpedantic -DMB_NO_PREVIEW src/mini_blender.c -o mini_blender_pi -lm
    fi
fi

base_chunk=$((total_frames / 5))
rem=$((total_frames % 5))
cur_start="$start_frame"
t_all_start=$(date +%s)
total_ppm=0

for idx in 1 2 3 4 5; do
    chunk_n="$base_chunk"
    if [ "$idx" -le "$rem" ]; then
        chunk_n=$((chunk_n + 1))
    fi
    chunk_end=$((cur_start + chunk_n - 1))
    node_tag=$(printf "node_%02d" "$idx")
    node_out="${out_base}/${node_tag}"
    node_log="logs/${node_tag}.log"

    node_cmd=(
        ./mini_blender_pi
        -f "$cur_start"
        -n "$chunk_n"
        -c "$cycle"
        -W "$width"
        -H "$height"
        -o "$node_out"
        -s "$scene"
    )
    if [ -n "$extra_args" ]; then
        # shellcheck disable=SC2206
        extra_arr=($extra_args)
        node_cmd+=("${extra_arr[@]}")
    fi
    cmd_str=$(printf '%q ' "${node_cmd[@]}")
    cmd_str="${cmd_str% }"

    echo "[Node ${idx}/5 -> ${node_tag}] frames ${cur_start}..${chunk_end} (count=${chunk_n}) res=${width}x${height} | out=${node_out}"
    echo "  CMD: ${cmd_str}"

    if [ "$mode" = "run" ]; then
        mkdir -p "$node_out"
        t_n0=$(date +%s)
        {
            echo "=== ${node_tag} START: $(date) ==="
            "${node_cmd[@]}"
            n_files=$(find "$node_out" -maxdepth 1 -name 'frame_*.ppm' | wc -l)
            n_size=$(du -sh "$node_out" | awk '{print $1}')
            t_n1=$(date +%s)
            echo "Node ${node_tag} done in $((t_n1 - t_n0))s | PPMs: ${n_files} | Size: ${n_size}"
            echo "=== ${node_tag} END ==="
        } | tee "$node_log"
        got_ppm=$(find "$node_out" -maxdepth 1 -name 'frame_*.ppm' | wc -l)
        total_ppm=$((total_ppm + got_ppm))
    fi

    cur_start=$((cur_start + chunk_n))
done

t_all_end=$(date +%s)
echo "=== SPLIT SUMMARY ==="
echo "Resolution: ${width}x${height} | Scene: ${scene}"
if [ "$mode" = "run" ]; then
    echo "Total elapsed: $((t_all_end - t_all_start))s | Total PPM frames generated across 5 folders: ${total_ppm}"
else
    echo "Generated 5 node commands. Run with --run to execute locally across ${out_base}/node_01..node_05."
fi

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p renders logs scenes
parallel_mode=0
frames_per_job=30
scene_default="scenes/kungfu_temple_blocking.xml"
jobs=("job_alpha:30:scenes/kungfu_temple_blocking.xml" "job_beta:30:scenes/blender_like_scene.xml" "job_gamma:30:scenes/kungfu_temple_blocking.xml")
while [ "$#" -gt 0 ]; do case "$1" in --parallel) parallel_mode=1; shift ;; -n) frames_per_job="$2"; shift 2 ;; -s) scene_default="$2"; shift 2 ;; --jobs) IFS="," read -r -a jobs <<< "$2"; shift 2 ;; *) echo "Unknown option: $1" >&2; exit 1 ;; esac; done
pids=()
tags=()
failed=0
echo "=== MULTI-JOB LAUNCHER | parallel=${parallel_mode} | count=${#jobs[@]} ==="
for spec in "${jobs[@]}"; do IFS=":" read -r j_tag j_frames j_scene <<< "$spec"; j_tag="${j_tag:-job_unnamed}"; j_frames="${j_frames:-$frames_per_job}"; j_scene="${j_scene:-$scene_default}"; echo "Starting job='${j_tag}' frames=${j_frames} scene=${j_scene} -> renders/${j_tag}"; if [ "$parallel_mode" -eq 1 ]; then ./mb_server_run.sh "$j_frames" "$j_scene" "$j_tag" & pids+=("$!"); tags+=("$j_tag"); else if ! ./mb_server_run.sh "$j_frames" "$j_scene" "$j_tag"; then echo "FAIL: job '${j_tag}' exited with error." >&2; failed=$((failed + 1)); fi; fi; done
if [ "$parallel_mode" -eq 1 ]; then for idx in "${!pids[@]}"; do pid="${pids[$idx]}"; j_tag="${tags[$idx]}"; if ! wait "$pid"; then echo "FAIL: parallel job '${j_tag}' (PID ${pid}) failed." >&2; failed=$((failed + 1)); else echo "OK: parallel job '${j_tag}' completed."; fi; done; fi
if [ "$failed" -gt 0 ]; then echo "=== MULTI-JOB FINISHED WITH ${failed} FAILURE(S) ===" >&2; exit 1; fi
echo "=== ALL ${#jobs[@]} JOBS COMPLETED SUCCESSFULLY ==="

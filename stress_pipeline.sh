#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

stress_root="renders/stress_lab"
rm -rf "$stress_root"
mkdir -p "$stress_root"

pass_count=0
fail_count=0

run_check() {
    local name="$1"
    shift
    echo "--- [TEST] ${name} ---"
    if "$@"; then
        echo "[PASS] ${name}"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] ${name}"
        fail_count=$((fail_count + 1))
    fi
}

# 1. Precedence check: CLI -W/-H vs MB_WIDTH/MB_HEIGHT vs --profile
t1_precedence() {
    local out
    out=$(MB_WIDTH=800 MB_HEIGHT=600 ./pipeline_run.sh --profile server --plan -t 10 -W 1024 -H 576 2>&1 || true)
    echo "$out" | tail -n 12
    # Check if CLI -W 1024 actually wins or gets clobbered by env/profile
    echo "$out" | grep -q "1024x576"
}

# 2. Non-divisible frame count across 5 nodes + dynamic HD PPM collection
t2_non_divisible_hd() {
    local base="${stress_root}/t2_nodes"
    local merged="${stress_root}/t2_merged"
    mkdir -p "$base" "$merged"
    ./split_5_nodes.sh --run -f 0 -t 13 -c 60 -W 1280 -H 720 -s scenes/mega_stress_scene.xml -o "$base" >/dev/null
    local out
    out=$(./collect_5_nodes.sh -b "$base" -o "$merged" -w 1280 -h 720)
    echo "$out"
    echo "$out" | grep -q "Valid frames collected: 13 | Invalid frames skipped: 0 | Sequence gaps: 0"
}

# 3. Corrupted PPM header, truncated pixels, and frame gap detection
t3_corrupt_and_gaps() {
    local base="${stress_root}/t3_nodes"
    local merged="${stress_root}/t3_merged"
    mkdir -p "${base}/node_01" "${base}/node_02" "$merged"

    # Valid frame_0000.ppm (16x16 P6)
    python3 -c '
with open("'"${base}/node_01/frame_0000.ppm"'", "wb") as f:
    f.write(b"P6\n16 16\n255\n" + b"\xaa\x55\x00" * (16 * 16))
'
    # Gap: jump to frame_0003.ppm (missing 1 and 2)
    python3 -c '
with open("'"${base}/node_01/frame_0003.ppm"'", "wb") as f:
    f.write(b"P6\n16 16\n255\n" + b"\x11\x22\x33" * (16 * 16))
'
    # Truncated pixel body (bad size)
    printf "P6\n16 16\n255\nshort" > "${base}/node_02/frame_0004.ppm"
    # Empty file
    : > "${base}/node_02/frame_0005.ppm"
    # Invalid magic
    printf "P3\n16 16\n255\n0 0 0\n" > "${base}/node_02/frame_0006.ppm"

    local out
    out=$(./collect_5_nodes.sh -b "$base" -o "$merged")
    echo "$out"
    echo "$out" | grep -q "Valid frames collected: 2 | Invalid frames skipped: 3 | Sequence gaps: 1"
}

# 4. Space in scene/output path + hook failure propagation
t4_spaces_and_hook_fail() {
    local space_dir="${stress_root}/dir with spaces"
    mkdir -p "$space_dir"
    local out
    out=$(HOOK_POST_COLLECT="false" ./pipeline_run.sh --run -f 0 -t 5 -W 320 -H 240 -s scenes/mega_stress_scene.xml -o "$space_dir" 2>&1 || true)
    echo "$out" | tail -n 10
    # Expect non-zero exit when post-collect hook fails, and check space handling
    ! HOOK_POST_COLLECT="false" ./pipeline_run.sh --run -f 0 -t 5 -W 320 -H 240 -s scenes/mega_stress_scene.xml -o "$space_dir" >/dev/null 2>&1
}

# 5. Heavy burst: 120 frames at 1280x720 across 5 nodes + collect
t5_heavy_120_hd() {
    local base="${stress_root}/t5_nodes"
    local merged="${stress_root}/t5_merged"
    local t0 t1 dt
    t0=$(date +%s)
    ./split_5_nodes.sh --run -f 0 -t 120 -c 60 -W 1280 -H 720 -s scenes/mega_stress_scene.xml -o "$base" >/dev/null
    ./collect_5_nodes.sh -b "$base" -o "$merged" -w 1280 -h 720 >/dev/null
    t1=$(date +%s)
    dt=$((t1 - t0))
    local count
    count=$(find "$merged" -maxdepth 1 -name 'frame_*.ppm' | wc -l)
    echo "120 HD frames split+collected in ${dt}s | merged_count=${count}"
    [ "$count" -eq 120 ]
}

run_check "1. Precedence (CLI vs ENV vs Profile)" t1_precedence
run_check "2. Non-divisible frames (13) + 1280x720 collect" t2_non_divisible_hd
run_check "3. Corrupted PPMs + gaps + empty/bad header" t3_corrupt_and_gaps
run_check "4. Paths with spaces + failing hook propagation" t4_spaces_and_hook_fail
run_check "5. Heavy load (120 frames @ 1280x720)" t5_heavy_120_hd

echo "========================================="
echo "STRESS SUMMARY: PASS=${pass_count} | FAIL=${fail_count}"
echo "Cleaning up ${stress_root}..."
rm -rf "$stress_root"
echo "========================================="

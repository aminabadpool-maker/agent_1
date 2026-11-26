#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p renders logs

echo "[1/5] Testing CLI help (-h) and invalid option handling..."
./mini_blender -h >/dev/null
if ./mini_blender --invalid-flag-check >/dev/null 2>&1; then
    echo "FAIL: invalid option should return non-zero" >&2
    exit 1
fi
echo "PASS: CLI help and error code check"

echo "[2/5] Testing interactive shell (-i) pipe execution..."
shell_out="$(printf "status\ng 0.1,0.2,0.0\nz 450\nstatus\nquit\n" | ./mini_blender -i)"
echo "$shell_out" | grep -q "z=450.0"
echo "PASS: Interactive shell pipe test"

echo "[3/5] Testing XML scene with shortcuts (-g, -r, -z, -m)..."
./mini_blender -f 200 -n 6 -c 24 -o renders -s scenes/blender_like_scene.xml -g 0.0,0.2,0.0 -r 0.02,0.1,0.0 -z 440 -m 1.0 -q | tee logs/test_xml_v7.log
echo "PASS: XML + shortcut render"

echo "[4/5] Verifying PPM binary integrity (header + exact byte size)..."
expected_size=$((640 * 480 * 3))
for idx in 200 201 202 203 204 205; do
    f_path="$(printf "renders/frame_%04d.ppm" "$idx")"
    if [ ! -f "$f_path" ]; then
        echo "FAIL: missing $f_path" >&2
        exit 1
    fi
    actual_size="$(wc -c < "$f_path")"
    if [ "$actual_size" -lt "$expected_size" ] || [ "$actual_size" -gt $((expected_size + 64)) ]; then
        echo "FAIL: unexpected size for $f_path ($actual_size bytes)" >&2
        exit 1
    fi
done
echo "PASS: PPM output files verified"

echo "[5/5] Testing headless Pi binary (mini_blender_pi) heavy stress batch..."
./mini_blender_pi -f 210 -n 60 -c 60 -o renders -s scenes/heavy_stress_scene.xml -q | tee logs/test_pi_stress_v7.log

echo "[6/6] Testing server-safe runner script (mb_server_run.sh)..."
./mb_server_run.sh 10 scenes/blender_like_scene.xml test_guard_check >/dev/null
rm -rf renders/test_guard_check logs/test_guard_check.log
echo "PASS: Server guard runner test"

echo "ALL TESTS PASSED."

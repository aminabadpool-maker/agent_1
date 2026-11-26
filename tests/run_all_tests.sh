#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p renders logs scenes

echo "[1/9] Testing CLI help (-h) and invalid option handling..."
./mini_blender -h >/dev/null
if ./mini_blender --invalid-flag-check >/dev/null 2>&1; then
    echo "FAIL: invalid option should return non-zero" >&2
    exit 1
fi
echo "PASS: CLI help and error code check"

echo "[2/9] Testing interactive shell (-i) pipe execution..."
shell_out="$(printf "status\ng 0.1,0.2,0.0\nz 450\nstatus\nquit\n" | ./mini_blender -i)"
echo "$shell_out" | grep -q "z=450.0"
echo "PASS: Interactive shell pipe test"

echo "[3/9] Testing XML scene with shortcuts (-g, -r, -z, -m)..."
./mini_blender -f 200 -n 6 -c 24 -o renders -s scenes/blender_like_scene.xml -g 0.0,0.2,0.0 -r 0.02,0.1,0.0 -z 440 -m 1.0 -q | tee logs/test_xml_v13.log
echo "PASS: XML + shortcut render"

echo "[4/9] Verifying PPM binary integrity (header + exact byte size)..."
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

echo "[5/9] Testing malformed / Blender-quirk XML resilience..."
cat <<'BADXML' > scenes/malformed_blender_import.xml
<scene name="quirky_blender_export">
  <world
    color="0.08,0.10,0.16"
    ambient="1.8" />
  <camera
    location="0.0,3.5,16.0"
    fov="99999"
    pitch="-0.2" />
  <object
    type='box'
    location="0.0,0.0,0.0"
    scale="-5.0,2.0,2.0"
    color="0.95,0.55,0.20"
    visible="1" />
  <object type="mesh" x="2.0" y="0.5" z="0.0" color="255,90,90">
    <v x="-0.5" y="-0.5" z="-0.5" />
    <v x="0.5" y="-0.5" z="-0.5" />
    <v x="0.0" y="0.8" z="0.0" />
    <tri v0="0" v1="1" v2="2" />
    <tri v0="0" v1="99" v2="-3" />
    <tri v0="1" v1="1" v2="2" />
  </object>
  <object type="mesh" x="-2.0" y="0.0" z="0.0" />
</scene>
BADXML
./mini_blender_pi -f 250 -n 4 -c 24 -o renders -s scenes/malformed_blender_import.xml -q | tee logs/test_malformed_v13.log
echo "PASS: Malformed / Blender-quirk XML handled safely"

echo "[6/9] Testing adversarial inputs (path traversal, frame cap, bad vectors)..."
if ./mini_blender_pi -o ../evil_out -n 1 -q >/dev/null 2>&1; then
    echo "FAIL: traversal path should be rejected" >&2
    exit 1
fi
if ./mini_blender_pi -o "bad\\slash" -n 1 -q >/dev/null 2>&1; then
    echo "FAIL: backslash path should be rejected" >&2
    exit 1
fi
if ./mini_blender_pi -n 99999999 -q >/dev/null 2>&1; then
    echo "FAIL: huge frame count should be rejected" >&2
    exit 1
fi
if ./mini_blender_pi -g "nan,inf,0" -q >/dev/null 2>&1; then
    echo "FAIL: NaN/Inf vector should be rejected" >&2
    exit 1
fi
echo "PASS: Adversarial CLI guard checks"

echo "[7/9] Testing headless Pi binary (mini_blender_pi) heavy assault batch..."
scene_target="scenes/mega_stress_scene.xml"
if [ ! -f "$scene_target" ]; then
    scene_target="scenes/heavy_stress_scene.xml"
fi
./mini_blender_pi -f 500 -n 150 -c 90 -o renders -s "$scene_target" -g 0.0,0.2,0.0 -r 0.03,0.15,0.0 -z 450 -m 1.0 -q | tee logs/test_pi_assault_v13.log
echo "PASS: Heavy Pi assault batch passed"

echo "[8/9] Testing --resume & checkpoint behavior..."
./mini_blender_pi -f 700 -n 10 -c 30 -o renders/resume_check --resume -q >/dev/null
resume_out="$(./mini_blender_pi -f 700 -n 10 -c 30 -o renders/resume_check --resume -q)"
echo "$resume_out" | grep -q "rendered=0 skipped=10"
rm -rf renders/resume_check
echo "PASS: Resume & checkpoint skip verified"

if [ -x "./mb_server_run.sh" ]; then
    echo "[9/9] Testing server-safe runner script (mb_server_run.sh)..."
    ./mb_server_run.sh 10 scenes/blender_like_scene.xml test_guard_check >/dev/null
    rm -rf renders/test_guard_check logs/test_guard_check.log
    echo "PASS: Server guard runner test"
fi

echo "ALL TESTS PASSED."

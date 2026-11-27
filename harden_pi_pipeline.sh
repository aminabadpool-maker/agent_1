#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1] Inspecting resolution & frame handling in src/mini_blender.c ==="
python3 - <<'PY'
from pathlib import Path
import re

p = Path("src/mini_blender.c")
lines = p.read_text(encoding="utf-8").splitlines()
pat = re.compile(r"\b(640|480|width|height|frames|num_frames|max_frames|total_frames|end_frame)\b")
matches = [(idx + 1, line) for idx, line in enumerate(lines) if pat.search(line)]
print(f"Found {len(matches)} relevant lines in src/mini_blender.c:")
for ln, content in matches[:60]:
    print(f"  L{ln}: {content}")
if len(matches) > 60:
    print(f"  ... ({len(matches) - 60} more lines)")
PY

echo "=== [2] Ensuring clean, idempotent scene transform guards in src/mini_blender.c ==="
python3 - <<'PY'
import re
from pathlib import Path

src_path = Path("src/mini_blender.c")
src = src_path.read_text(encoding="utf-8")

def extract_struct_fields(text, struct_name):
    if not struct_name:
        return set()
    patterns = [
        rf"typedef\s+struct(?:\s+\w+)?\s*\{{(.*?)\}}\s*{re.escape(struct_name)}\s*;",
        rf"struct\s+{re.escape(struct_name)}\s*\{{(.*?)\}}\s*;",
    ]
    for pat in patterns:
        m = re.search(pat, text, re.S)
        if m:
            fields = set()
            for line in m.group(1).splitlines():
                line = re.sub(r"//.*", "", line).strip()
                if not line or line.endswith("{") or line.endswith("}"):
                    continue
                line = line.rstrip(";")
                line = re.sub(r"\[[^\]]*\]", "", line).strip()
                parts = line.replace("*", " ").split()
                if parts:
                    fields.add(parts[-1])
            return fields
    return set()

scene_fields = extract_struct_fields(src, "Scene")
light_fields = extract_struct_fields(src, "Light")

obj_type_name = None
for pat in [
    r"typedef\s+struct(?:\s+\w+)?\s*\{(.*?)\}\s*Scene\s*;",
    r"struct\s+Scene\s*\{(.*?)\}\s*;",
]:
    m_scene = re.search(pat, src, re.S)
    if m_scene:
        for sline in m_scene.group(1).splitlines():
            sline_clean = re.sub(r"//.*", "", sline).strip().rstrip(";")
            if re.search(r"\bobjs\s*\[", sline_clean):
                tokens = re.sub(r"\[[^\]]*\]", "", sline_clean).replace("*", " ").split()
                if len(tokens) >= 2:
                    obj_type_name = tokens[1] if tokens[0] == "struct" else tokens[0]
                break
        if obj_type_name:
            break

obj_fields = extract_struct_fields(src, obj_type_name)
obj_vec_fields = [(f, fb) for f, fb in (("pos", "zero"), ("rot", "zero"), ("offset", "zero"), ("scale", "one")) if f in obj_fields]
has_obj_scale = "scale" in obj_fields
need_one = has_obj_scale or any(fb == "one" for _, fb in obj_vec_fields)

fn_lines = [
    "static void mb_scene_sanitize_transforms(Scene *s) {",
    "    if (!s) return;",
    "    Vec3 zero = {0.0f, 0.0f, 0.0f};",
]
if need_one:
    fn_lines.append("    Vec3 one = {1.0f, 1.0f, 1.0f};")

obj_count_field = "num_objs" if "num_objs" in scene_fields else ("obj_count" if "obj_count" in scene_fields else None)
if obj_count_field and "objs" in scene_fields and obj_vec_fields:
    fn_lines.append(f"    for (int i = 0; i < s->{obj_count_field}; i++) {{")
    fn_lines.append("        __typeof__(s->objs[0]) *o = &s->objs[i];")
    for f_name, fb in obj_vec_fields:
        fn_lines.append(f"        o->{f_name} = mb_vec3_sane(o->{f_name}, {fb});")
    if has_obj_scale:
        fn_lines.append("        if (fabsf(o->scale.x) < 1e-4f) o->scale.x = (o->scale.x < 0.0f ? -1e-4f : 1e-4f);")
        fn_lines.append("        if (fabsf(o->scale.y) < 1e-4f) o->scale.y = (o->scale.y < 0.0f ? -1e-4f : 1e-4f);")
        fn_lines.append("        if (fabsf(o->scale.z) < 1e-4f) o->scale.z = (o->scale.z < 0.0f ? -1e-4f : 1e-4f);")
    fn_lines.append("    }")

light_count_field = "num_lights" if "num_lights" in scene_fields else ("light_count" if "light_count" in scene_fields else None)
if light_count_field and "lights" in scene_fields:
    light_vec_fields = [f for f in ("pos", "dir") if f in light_fields]
    has_intensity = "intensity" in light_fields
    if light_vec_fields or has_intensity:
        fn_lines.append(f"    for (int i = 0; i < s->{light_count_field}; i++) {{")
        fn_lines.append("        Light *l = &s->lights[i];")
        for f_name in light_vec_fields:
            fn_lines.append(f"        l->{f_name} = mb_vec3_sane(l->{f_name}, zero);")
        if has_intensity:
            fn_lines.append("        l->intensity = mb_finf(l->intensity, 1.0f);")
            fn_lines.append("        if (l->intensity < 0.0f) l->intensity = 0.0f;")
        fn_lines.append("    }")

fn_lines.append("}")
helper_code = "\n" + "\n".join(fn_lines) + "\n"

if "mb_scene_sanitize_transforms(" not in src:
    anchor = "static void mb_cam_apply_target_and_roll("
    idx = src.find(anchor)
    if idx != -1:
        end_fn = src.find("\n}\n", idx)
        if end_fn != -1:
            src = src[:end_fn + 3] + helper_code + src[end_fn + 3:]

call_snippet = "        mb_scene_sanitize_transforms(s);"
if call_snippet not in src:
    target_anchor = "mb_cam_apply_target_and_roll(&s->cam);"
    t_idx = src.find(target_anchor)
    if t_idx != -1:
        line_end = src.find("\n", t_idx)
        if line_end != -1:
            src = src[:line_end + 1] + call_snippet + "\n" + src[line_end + 1:]

src_path.write_text(src, encoding="utf-8")
print("Sanitizer check complete.")
PY

echo "=== [3] Adding optional MP4 hook to mb_server_run.sh if present ==="
python3 - <<'PY'
from pathlib import Path

srv = Path("mb_server_run.sh")
if srv.exists():
    txt = srv.read_text(encoding="utf-8")
    hook = '\nif [ -x ./ppm_to_mp4.sh ] && command -v ffmpeg >/dev/null 2>&1; then\n  for d in renders/*/; do\n    [ -d "$d" ] || continue\n    if ls "$d"/frame_*.ppm >/dev/null 2>&1; then\n      ./ppm_to_mp4.sh -i "${d%/}" -o "${d%/}/output.mp4" -f 24 --crf 20 >/dev/null 2>&1 || true\n    fi\n  done\nfi\n'
    if "ppm_to_mp4.sh" not in txt:
        srv.write_text(txt + hook, encoding="utf-8")
        print("Updated mb_server_run.sh with optional MP4 hook.")
    else:
        print("mb_server_run.sh already contains ppm_to_mp4.sh hook.")
else:
    print("mb_server_run.sh not found; skipped.")
PY

echo "=== [4] Building with -Werror (standard + MB_NO_PREVIEW) ==="
make clean
cc -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -mtune=native src/mini_blender.c -o mini_blender -lm
cc -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -mtune=native -DMB_NO_PREVIEW src/mini_blender.c -o mini_blender_pi -lm

echo "=== [5] Running test suite & guard/cleanup check ==="
if [ -x ./tests/run_all_tests.sh ]; then
  ./tests/run_all_tests.sh
fi

work_dir="./.tmp_harden_$$"
out_dir="${work_dir}/out"
mkdir -p "$out_dir"
scene_file="${work_dir}/guard_test.xml"
cat <<'XML' > "$scene_file"
<scene width="320" height="240" frames="4">
  <camera pos="0 0 6" target="0 0 0" roll="0.2" fov="60"/>
  <light pos="nan inf -nan" dir="0 -1 0" intensity="-5"/>
  <object type="cube" pos="nan 1 0" rot="0 inf 0" scale="0 nan 1"/>
</scene>
XML
./mini_blender_pi -s "$scene_file" -o "$out_dir" -f 4
./ppm_to_mp4.sh -i "$out_dir" -o "${out_dir}/check.mp4" -f 12 --crf 22 --clean-ppm
echo "PPM left: $(find "$out_dir" -maxdepth 1 -name 'frame_*.ppm' | wc -l)"
rm -rf "$work_dir"

echo "=== [6] Git & untracked files audit ==="
git status -s

# Guarded cleanup helper using safe_path_guard
safe_rm_rf_guarded() {
  local target_dir="${1:-}"
  local allowed_root="${2:-}"
  local repo_root
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  if [ -f "$script_dir/tools/safe_path_guard.c" ]; then
    repo_root="$script_dir"
  else
    repo_root="$(cd "$script_dir/.." && pwd)"
  fi
  local guard_src="$repo_root/tools/safe_path_guard.c"
  local guard_tmp="${TMPDIR:-/tmp}/agent1_safe_path_guard_$$"

  [ -n "$target_dir" ] && [ -n "$allowed_root" ] || {
    echo "[SAFE-RM BLOCK] Missing target_dir or allowed_root" >&2
    return 1
  }

  gcc -std=c11 -O2 -Wall -Wextra -Werror "$guard_src" -o "$guard_tmp"
  if ! "$guard_tmp" "$target_dir" "$allowed_root" >/dev/null; then
    rm -f "$guard_tmp"
    echo "[SAFE-RM BLOCK] Blocked rm -rf on '$target_dir' (root='$allowed_root')" >&2
    return 1
  fi
  rm -f "$guard_tmp"
  rm -rf "$target_dir"
}

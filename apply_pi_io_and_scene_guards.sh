#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

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
    body = None
    for pat in patterns:
        m = re.search(pat, text, re.S)
        if m:
            body = m.group(1)
            break
    if body is None:
        return set()
    fields = set()
    for line in body.splitlines():
        line = re.sub(r"//.*", "", line).strip()
        if not line or line.endswith("{") or line.endswith("}"):
            continue
        line = line.rstrip(";").strip()
        line = re.sub(r"\[[^\]]*\]", "", line).strip()
        parts = line.replace("*", " ").split()
        if parts:
            fields.add(parts[-1])
    return fields

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

lines = [
    "static void mb_scene_sanitize_transforms(Scene *s) {",
    "    if (!s) return;",
    "    Vec3 zero = {0.0f, 0.0f, 0.0f};",
]
if need_one:
    lines.append("    Vec3 one = {1.0f, 1.0f, 1.0f};")

obj_count_field = "num_objs" if "num_objs" in scene_fields else ("obj_count" if "obj_count" in scene_fields else None)
if obj_count_field and "objs" in scene_fields and obj_vec_fields:
    lines.append(f"    for (int i = 0; i < s->{obj_count_field}; i++) {{")
    lines.append("        __typeof__(s->objs[0]) *o = &s->objs[i];")
    for f_name, fb in obj_vec_fields:
        lines.append(f"        o->{f_name} = mb_vec3_sane(o->{f_name}, {fb});")
    if has_obj_scale:
        lines.append("        if (fabsf(o->scale.x) < 1e-4f) o->scale.x = (o->scale.x < 0.0f ? -1e-4f : 1e-4f);")
        lines.append("        if (fabsf(o->scale.y) < 1e-4f) o->scale.y = (o->scale.y < 0.0f ? -1e-4f : 1e-4f);")
        lines.append("        if (fabsf(o->scale.z) < 1e-4f) o->scale.z = (o->scale.z < 0.0f ? -1e-4f : 1e-4f);")
    lines.append("    }")

light_count_field = "num_lights" if "num_lights" in scene_fields else ("light_count" if "light_count" in scene_fields else None)
if light_count_field and "lights" in scene_fields:
    light_vec_fields = [f for f in ("pos", "dir") if f in light_fields]
    has_intensity = "intensity" in light_fields
    if light_vec_fields or has_intensity:
        lines.append(f"    for (int i = 0; i < s->{light_count_field}; i++) {{")
        lines.append("        Light *l = &s->lights[i];")
        for f_name in light_vec_fields:
            lines.append(f"        l->{f_name} = mb_vec3_sane(l->{f_name}, zero);")
        if has_intensity:
            lines.append("        l->intensity = mb_finf(l->intensity, 1.0f);")
            lines.append("        if (l->intensity < 0.0f) l->intensity = 0.0f;")
        lines.append("    }")

lines.append("}")
helper_code = "\n" + "\n".join(lines) + "\n"

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

ppm_path = Path("ppm_to_mp4.sh")
if ppm_path.exists():
    psrc = ppm_path.read_text(encoding="utf-8")
    if "--clean-ppm" not in psrc:
        psrc = psrc.replace('crf="20"', 'crf="20"\nclean_ppm="0"', 1)
        psrc = psrc.replace(
            '--crf) crf="$2"; shift 2 ;;',
            '--crf) crf="$2"; shift 2 ;;\n        --clean-ppm) clean_ppm="1"; shift 1 ;;',
            1
        )
        tail_target = 'echo "Done -> $out_file"'
        clean_block = """echo "Done -> $out_file"
if [ "$clean_ppm" = "1" ] && [ -s "$out_file" ]; then
    rm -f "${in_dir}"/frame_*.ppm
    echo "Cleaned intermediate PPM frames in $in_dir"
fi"""
        if tail_target in psrc:
            psrc = psrc.replace(tail_target, clean_block, 1)
        ppm_path.write_text(psrc, encoding="utf-8")
PY

make clean && \
cc -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -mtune=native src/mini_blender.c -o mini_blender -lm && \
cc -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -mtune=native -DMB_NO_PREVIEW src/mini_blender.c -o mini_blender_pi -lm

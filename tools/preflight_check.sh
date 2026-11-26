#!/usr/bin/env bash
set -euo pipefail

out_base="${1:-renders/srv_run}"
profile_name="${2:-}"
errors=0

check_cmd() {
    local cmd="$1"
    if command -v "$cmd" >/dev/null 2>&1; then
        printf "[PREFLIGHT OK] command '%s' -> %s\n" "$cmd" "$(command -v "$cmd")"
    else
        printf "[PREFLIGHT ERR] missing required command: '%s'\n" "$cmd" >&2
        errors=$((errors + 1))
    fi
}

check_cmd "ffmpeg"
check_cmd "awk"
check_cmd "wc"
check_cmd "sha256sum"

if [ -n "$profile_name" ]; then
    profile_file="profiles/${profile_name}.env"
    if [ -f "$profile_file" ]; then
        printf "[PREFLIGHT OK] profile found: %s\n" "$profile_file"
    else
        printf "[PREFLIGHT ERR] profile not found: %s\n" "$profile_file" >&2
        errors=$((errors + 1))
    fi
fi

# جستجو برای باینری رندر اگر در ریشه مخزن ساخته شده باشد
renderer_found=0
for bin_candidate in ./mini_blender_pi ./render_node ./scene_render; do
    if [ -x "$bin_candidate" ]; then
        printf "[PREFLIGHT OK] renderer binary ready: %s\n" "$bin_candidate"
        renderer_found=1
        break
    fi
done

if [ "$renderer_found" -eq 0 ]; then
    printf "[PREFLIGHT WARN] no executable renderer binary found in repo root (check build before full run)\n" >&2
fi

if mkdir -p "$out_base" 2>/dev/null && [ -w "$out_base" ]; then
    printf "[PREFLIGHT OK] output directory writable: %s\n" "$out_base"
else
    printf "[PREFLIGHT ERR] output directory not writable: %s\n" "$out_base" >&2
    errors=$((errors + 1))
fi

if [ "$errors" -gt 0 ]; then
    printf "[PREFLIGHT FAIL] %d critical issue(s) found\n" "$errors" >&2
    exit 1
fi

printf "[PREFLIGHT PASS] Environment and paths are ready\n"

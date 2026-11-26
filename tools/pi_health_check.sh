#!/usr/bin/env bash
set -euo pipefail

target_dir="${1:-renders/pi_run}"
min_free_mb="${2:-256}"
warn_temp_c="${3:-75}"
errors=0

mkdir -p "$target_dir"

free_kb="$(df -Pk "$target_dir" | awk 'NR == 2 { print $4 }')"
if ! [[ "$free_kb" =~ ^[0-9]+$ ]]; then
    printf "[HEALTH ERR] Cannot read free disk space for '%s'\n" "$target_dir" >&2
    exit 1
fi

free_mb="$((free_kb / 1024))"
if [ "$free_mb" -lt "$min_free_mb" ]; then
    printf "[HEALTH ERR] Low disk space on '%s': %dMB free (need >= %dMB)\n" "$target_dir" "$free_mb" "$min_free_mb" >&2
    errors=$((errors + 1))
else
    printf "[HEALTH OK] Disk free on '%s': %dMB (min %dMB)\n" "$target_dir" "$free_mb" "$min_free_mb"
fi

if [ -r /proc/meminfo ]; then
    avail_kb="$(awk '/^MemAvailable:/ { print $2; exit }' /proc/meminfo)"
    if [[ "${avail_kb:-}" =~ ^[0-9]+$ ]]; then
        avail_mb="$((avail_kb / 1024))"
        printf "[HEALTH OK] Available RAM: %dMB\n" "$avail_mb"
    fi
fi

thermal_file="/sys/class/thermal/thermal_zone0/temp"
if [ -r "$thermal_file" ]; then
    raw_temp="$(cat "$thermal_file" 2>/dev/null || true)"
    if [[ "$raw_temp" =~ ^[0-9]+$ ]]; then
        temp_c="$((raw_temp / 1000))"
        if [ "$temp_c" -ge "$warn_temp_c" ]; then
            printf "[HEALTH WARN] High CPU temperature: %dC (threshold %dC)\n" "$temp_c" "$warn_temp_c" >&2
        else
            printf "[HEALTH OK] CPU temperature: %dC\n" "$temp_c"
        fi
    fi
fi

if [ "$errors" -gt 0 ]; then
    printf "[HEALTH FAIL] %d critical issue(s) detected\n" "$errors" >&2
    exit 1
fi

printf "[HEALTH PASS] Node health ready for render\n"

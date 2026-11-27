#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_MB="$REPO_DIR/src/mini_blender.c"
TOOLS_DIR="$REPO_DIR/tools"
TMP_BASE="${TMPDIR:-/tmp}"
WORK_DIR="$(mktemp -d "$TMP_BASE/agent1_mb_real.XXXXXX")"
BIN_MB="$WORK_DIR/mini_blender"
BIN_SUM="$WORK_DIR/render_summary_report"
BIN_BATCH="$WORK_DIR/render_batch_report"

cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

gcc -std=c11 -O2 -Wall -Wextra -Werror "$SRC_MB" -lm -o "$BIN_MB"
gcc -std=c11 -O2 -Wall -Wextra -Werror "$TOOLS_DIR/render_summary_report.c" -o "$BIN_SUM"
gcc -std=c11 -O2 -Wall -Wextra -Werror "$TOOLS_DIR/render_batch_report.c" -o "$BIN_BATCH"

START_TS="$(date +%s)"
(
  cd "$WORK_DIR"
  "$BIN_MB" >/dev/null
)
END_TS="$(date +%s)"
ELAPSED=$((END_TS - START_TS))
[ "$ELAPSED" -lt 1 ] && ELAPSED=1

shopt -s nullglob
REAL_PPMS=("$WORK_DIR"/renders/*.ppm)
shopt -u nullglob

if [ "${#REAL_PPMS[@]}" -eq 0 ]; then
  echo "[ERROR] No PPM artifacts found under $WORK_DIR/renders" >&2
  exit 1
fi

for ppm in "${REAL_PPMS[@]}"; do
  base_name="$(basename "$ppm" .ppm)"
  "$BIN_SUM" "$base_name" "$ppm" "$ELAPSED" 16 10485760 >/dev/null
done

"$BIN_BATCH" "${REAL_PPMS[@]}"

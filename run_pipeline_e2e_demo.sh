#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${1:-$HOME/agent_1}"
TOOLS_DIR="$REPO_DIR/tools"
TMP_BASE="${TMPDIR:-/tmp}"
WORK_DIR="$(mktemp -d "$TMP_BASE/agent1_e2e.XXXXXX")"

cleanup() {
  rm -rf "$WORK_DIR"
  rm -f "$TOOLS_DIR/queue_validator" \
        "$TOOLS_DIR/render_batch_queue" \
        "$TOOLS_DIR/render_summary_report" \
        "$TOOLS_DIR/render_batch_report"
}
trap cleanup EXIT

for src in "queue_validator.c" "render_batch_queue.c" "render_summary_report.c" "render_batch_report.c"; do
  if [ ! -f "$TOOLS_DIR/$src" ]; then
    echo "[ERROR] Missing source file: $TOOLS_DIR/$src" >&2
    exit 1
  fi
done

echo "=== 1. COMPILING PIPELINE C11 TOOLS ==="
for tool in "queue_validator" "render_batch_queue" "render_summary_report" "render_batch_report"; do
  gcc -std=c11 -O2 -Wall -Wextra -Werror "$TOOLS_DIR/$tool.c" -o "$TOOLS_DIR/$tool"
done

QUEUE_FILE="$WORK_DIR/render_jobs.queue"
OUT_A="$WORK_DIR/shot_001.ppm"
OUT_B="$WORK_DIR/shot_002.ppm"
OUT_C="$WORK_DIR/shot_003.ppm"

cat << EOF > "$QUEUE_FILE"
# sample multi-priority render queue
job_lighting|${OUT_B}|10|18
job_hero_cam|${OUT_A}|1|35
job_compositing|${OUT_C}|20|12
EOF

echo "=== 2. VALIDATING QUEUE ==="
"$TOOLS_DIR/queue_validator" "$QUEUE_FILE"

echo "=== 3. PRIORITIZING QUEUE ORDER ==="
"$TOOLS_DIR/render_batch_queue" "$QUEUE_FILE"

echo "=== 4. SIMULATING RENDER ARTIFACTS & PER-JOB SUMMARY ==="
printf "P3\n2 2\n255\n255 120 40 10 20 30 40 50 60 70 80 90\n" > "$OUT_A"
"$TOOLS_DIR/render_summary_report" "job_hero_cam" "$OUT_A" 34 16 1048576

printf "P3\n2 2\n255\n15 25 35 45 55 65 75 85 95 105 115 125\n" > "$OUT_B"
"$TOOLS_DIR/render_summary_report" "job_lighting" "$OUT_B" 17 16 1048576

# Simulate a failed/empty render artifact for job_compositing to verify failure reporting
: > "$OUT_C"
set +e
"$TOOLS_DIR/render_summary_report" "job_compositing" "$OUT_C" 12 16 1048576
SUM_C_RC=$?
set -e
echo "job_compositing_summary_exit_code=$SUM_C_RC"

echo "=== 5. AGGREGATING BATCH REPORT ==="
set +e
"$TOOLS_DIR/render_batch_report" "$OUT_A" "$OUT_B" "$OUT_C"
BATCH_RC=$?
set -e
echo "batch_report_exit_code=$BATCH_RC"

if [ "$SUM_C_RC" -eq 0 ] || [ "$BATCH_RC" -eq 0 ]; then
  echo "[ERROR] Expected non-zero exit code when an artifact is empty/invalid." >&2
  exit 1
fi

echo "=== E2E DEMO COMPLETED SUCCESSFULLY ==="

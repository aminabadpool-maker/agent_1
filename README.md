# agent_1

Lightweight C11 task execution, safety-guard, and render verification suite for Termux/Android and Linux.

## Key Capabilities
- Strict C11 tools (-std=c11 -O2 -Wall -Wextra -Werror) including render_summary_report, render_batch_report, and render_metrics_guard.
- Multi-layer verification targets in Makefile: safety-check, e2e-check, mb-check, full-check, and stress-full-check.
- Deterministic status lines and non-zero exit codes on invalid frames or size drift.

## Quick Start
1. cd ~/agent_1
2. make full-check
3. make stress-full-check

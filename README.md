# agent_1 - Render Queue & Resource Guard

Lightweight C11 resource validation and batch-sizing guard for multi-job rendering pipelines.

## Overview
- tools/safe_batch.h: single source of truth for validation, disk queue checks, overflow checks, and 4K batch cap (64).
- tools/pi_guard.c: CLI guard checking RAM, thermal limits, and free disk via statvfs.
- Makefile & tools/test_final_guard.c: automated guard-check target and C11 unit tests.

## Build and Run
gcc -std=c11 -O2 -Wall -Wextra -I. -I./tools tools/pi_guard.c -o tools/pi_guard
make guard-check
./tools/pi_guard 3840 2160 300 2048 8 256 512 4096 75.0 ./out 20

## Sample Outputs
- Pass (20 jobs x 512MB): [GUARD PASS] ram=3799MB disk=46255MB temp=37.0C threads=8 batch=64
- Block (20 jobs x 2313MB > 46256MB): [GUARD BLOCK] rc=-2

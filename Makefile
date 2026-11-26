CC ?= cc
CFLAGS ?= -std=c11 -O3 -Wall -Wextra

PI_LIB_SRC := tools/pi_lib.c
PI_LIB_HDR := tools/pi_lib.h
HEALTH_SCRIPT := tools/pi_health_check.sh

W ?= 1280
H ?= 720
FRAMES ?= 300
RAM ?= 1001
CORES ?= 4
BUDGET ?= 500

TARGETS := tools/pi_budget tools/pi_lib_demo

.PHONY: all check health plan clean

all: $(TARGETS)

tools/pi_budget: tools/pi_budget.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_budget.c $(PI_LIB_SRC) -o $@

tools/pi_lib_demo: tools/pi_lib_demo.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_lib_demo.c $(PI_LIB_SRC) -o $@

check: all tools/run_budget_matrix.sh
	./tools/run_budget_matrix.sh

health:
	@if [ ! -f "$(HEALTH_SCRIPT)" ]; then \
		echo "[ERR] Missing $(HEALTH_SCRIPT)" >&2; \
		exit 1; \
	fi
	@chmod +x "$(HEALTH_SCRIPT)"
	./$(HEALTH_SCRIPT)

plan: health tools/pi_budget
	./tools/pi_budget "$(W)" "$(H)" "$(FRAMES)" "$(RAM)" "$(CORES)" "$(BUDGET)"

clean:
	rm -f $(TARGETS)

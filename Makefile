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
MAX_THREADS ?= 4
MAX_BATCH ?= 256
MIN_RAM_MB ?= 512
MIN_DISK_MB ?= 1024
MAX_TEMP_C ?= 75.0
WATCH_DIR ?= renders/pi_run
TELEMETRY_LOG ?= renders/pi_run/telemetry.jsonl

TARGETS := tools/pi_budget tools/pi_lib_demo tools/pi_telemetry tools/pi_guard tools/pi_stress

.PHONY: all check health plan telemetry log-summary guard stress safe-run clean

all: $(TARGETS)

tools/pi_budget: tools/pi_budget.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_budget.c $(PI_LIB_SRC) -o $@

tools/pi_lib_demo: tools/pi_lib_demo.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_lib_demo.c $(PI_LIB_SRC) -o $@

tools/pi_telemetry: tools/pi_telemetry.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_telemetry.c $(PI_LIB_SRC) -o $@

tools/pi_guard: tools/pi_guard.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_guard.c $(PI_LIB_SRC) -o $@

tools/pi_stress: tools/pi_stress.c $(PI_LIB_SRC) $(PI_LIB_HDR)
	$(CC) $(CFLAGS) tools/pi_stress.c $(PI_LIB_SRC) -lm -o $@

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

telemetry: tools/pi_telemetry
	./tools/pi_telemetry "$(W)" "$(H)" "$(FRAMES)" "$(BUDGET)" "$(MAX_THREADS)" "$(WATCH_DIR)" "$(TELEMETRY_LOG)"

log-summary:
	@if [ ! -f "$(TELEMETRY_LOG)" ]; then \
		echo "[ERR] Missing $(TELEMETRY_LOG)" >&2; \
		exit 1; \
	fi
	@awk -F'[,{}":]+' ' \
	BEGIN { \
		printf "%-22s %-7s %-8s %-9s %-7s %-8s %-12s\n", \
		       "TS", "HEALTHY", "RAM_MB", "DISK_MB", "TEMP_C", "THREADS", "BATCH_FRAMES" \
	} \
	NF > 0 { \
		ts=""; h=""; ram=""; disk=""; temp=""; thr=""; batch=""; \
		for (i=1; i<=NF; i++) { \
			gsub(/^[[:space:]]+|[[:space:]]+$$/, "", $$i); \
			if ($$i == "ts") { \
				y=$$(i+1); m=$$(i+2); s=$$(i+3); \
				gsub(/^[[:space:]]+|[[:space:]]+$$/, "", y); \
				gsub(/^[[:space:]]+|[[:space:]]+$$/, "", m); \
				gsub(/^[[:space:]]+|[[:space:]]+$$/, "", s); \
				if (y != "" && m != "" && s != "") ts = y ":" m ":" s; \
			} else if ($$i == "healthy") h = $$(i+1); \
			else if ($$i == "avail_ram_mb") ram = $$(i+1); \
			else if ($$i == "disk_free_mb") disk = $$(i+1); \
			else if ($$i == "temp_c") temp = $$(i+1); \
			else if ($$i == "threads") thr = $$(i+1); \
			else if ($$i == "batch_frames") batch = $$(i+1); \
		} \
		gsub(/^[[:space:]]+|[[:space:]]+$$/, "", h); \
		gsub(/^[[:space:]]+|[[:space:]]+$$/, "", ram); \
		gsub(/^[[:space:]]+|[[:space:]]+$$/, "", disk); \
		gsub(/^[[:space:]]+|[[:space:]]+$$/, "", temp); \
		gsub(/^[[:space:]]+|[[:space:]]+$$/, "", thr); \
		gsub(/^[[:space:]]+|[[:space:]]+$$/, "", batch); \
		if (ts != "") { \
			n++; \
			h_sum += (h+0); \
			r_val = (ram+0); r_sum += r_val; \
			if (n==1 || r_val < r_min) r_min = r_val; \
			if (n==1 || r_val > r_max) r_max = r_val; \
			t_val = (temp+0.0); t_sum += t_val; \
			if (n==1 || t_val < t_min) t_min = t_val; \
			if (n==1 || t_val > t_max) t_max = t_val; \
			b_val = (batch+0); b_sum += b_val; \
			if (n==1 || b_val < b_min) b_min = b_val; \
			if (n==1 || b_val > b_max) b_max = b_val; \
			rows[n] = sprintf("%-22s %-7s %-8s %-9s %-7s %-8s %-12s", ts, h, ram, disk, temp, thr, batch); \
		} \
	} \
	END { \
		if (n == 0) { print "No valid records found."; exit 1 } \
		start = (n > 10) ? (n - 9) : 1; \
		for (k=start; k<=n; k++) print rows[k]; \
		print ""; \
		printf "Records      : %d (healthy=%d)\n", n, h_sum; \
		printf "Avail RAM MB : min=%d avg=%.1f max=%d\n", r_min, r_sum/n, r_max; \
		printf "CPU Temp C   : min=%.1f avg=%.1f max=%.1f\n", t_min, t_sum/n, t_max; \
		printf "Batch Frames : min=%d avg=%.1f max=%d\n", b_min, b_sum/n, b_max; \
	}' "$(TELEMETRY_LOG)"

guard: tools/pi_guard
	./tools/pi_guard "$(W)" "$(H)" "$(FRAMES)" "$(BUDGET)" "$(MAX_THREADS)" "$(MAX_BATCH)" "$(MIN_RAM_MB)" "$(MIN_DISK_MB)" "$(MAX_TEMP_C)" "$(WATCH_DIR)"

stress: tools/pi_stress
	./tools/pi_stress

safe-run:
	@$(MAKE) guard && $(MAKE) telemetry && $(MAKE) plan

clean:
	rm -f $(TARGETS)

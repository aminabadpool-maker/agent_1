#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."

cat <<'C_SRC' > tools/pi_stress.c
#define _POSIX_C_SOURCE 200809L
#include "pi_lib.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

typedef struct {
    const char *name;
    int width;
    int height;
    int total_frames;
    uint64_t budget_mb;
    int max_threads_cap;
    int iterations;
} stress_case_t;

static long read_disk_free_mb(const char *path) {
    struct statvfs st;
    if (!path || statvfs(path, &st) != 0) {
        return -1;
    }
    unsigned long long free_bytes =
        (unsigned long long)st.f_bavail * (unsigned long long)st.f_frsize;
    return (long)(free_bytes / (1024ULL * 1024ULL));
}

static int read_cpu_temp_c(double *out_c) {
    const char *paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/devices/virtual/thermal/thermal_zone0/temp"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        FILE *fp = fopen(paths[i], "r");
        if (fp) {
            long milli = 0;
            if (fscanf(fp, "%ld", &milli) == 1) {
                fclose(fp);
                *out_c = (double)milli / 1000.0;
                return 1;
            }
            fclose(fp);
        }
    }
    return 0;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}

static void ensure_dir(const char *dir) {
    if (dir && dir[0] != '\0') {
        mkdir(dir, 0755);
    }
}

int main(void) {
    const char *out_dir = "renders/pi_run";
    const char *log_path = "renders/pi_run/stress.jsonl";
    ensure_dir(out_dir);

    PiCaps caps = pi_probe_system();
    if (caps.avail_ram_mb == 0ULL) caps.avail_ram_mb = 256ULL;
    if (caps.cpu_cores <= 0) caps.cpu_cores = 1;

    long disk_mb = read_disk_free_mb(out_dir);
    double temp_start = -1.0;
    int temp_ok = read_cpu_temp_c(&temp_start);

    printf("[STRESS INIT] cores=%ld ram=%" PRIu64 "MB disk=%ldMB temp=%.1fC\n",
           caps.cpu_cores, caps.avail_ram_mb, disk_mb, temp_ok ? temp_start : -1.0);

    stress_case_t cases[] = {
        { "hd_300_500mb",      1280,  720,  300,  500ULL, 4, 200000 },
        { "fhd_600_512mb",     1920, 1080,  600,  512ULL, 4, 200000 },
        { "fhd_1200_1024mb",   1920, 1080, 1200, 1024ULL, 8, 200000 },
        { "qhd_600_1024mb",    2560, 1440,  600, 1024ULL, 8, 200000 },
        { "4k_300_1024mb",     3840, 2160,  300, 1024ULL, 8, 200000 },
        { "4k_1000_2048mb",    3840, 2160, 1000, 2048ULL, 8, 200000 },
        { "4k_2000_tight_256", 3840, 2160, 2000,  256ULL, 4, 200000 }
    };
    size_t num_cases = sizeof(cases) / sizeof(cases[0]);

    FILE *logf = fopen(log_path, "a");
    if (!logf) {
        fprintf(stderr, "[ERR] Cannot open %s: %s\n", log_path, strerror(errno));
        return 1;
    }

    int feasible_count = 0;
    uint64_t max_feasible_pixels = 0ULL;
    const char *max_feasible_case = "none";

    for (size_t i = 0; i < num_cases; ++i) {
        double t_now = -1.0;
        int t_valid = read_cpu_temp_c(&t_now);
        if (t_valid && t_now > 80.0) {
            fprintf(stderr, "[STRESS ABORT] Thermal guard triggered: %.1fC > 80.0C\n", t_now);
            break;
        }

        PiPolicy pol;
        pol.width = cases[i].width;
        pol.height = cases[i].height;
        pol.total_frames = cases[i].total_frames;
        pol.ram_budget_mb = cases[i].budget_mb;
        pol.max_threads_cap = cases[i].max_threads_cap;

        PiAllocation last_alloc;
        memset(&last_alloc, 0, sizeof(last_alloc));
        uint64_t ppm_bytes = pi_estimate_ppm_bytes(pol.width, pol.height);

        double t0 = now_ms();
        for (int it = 0; it < cases[i].iterations; ++it) {
            pol.total_frames = cases[i].total_frames + (it & 1);
            last_alloc = pi_plan_dynamic(caps, pol);
        }
        double elapsed_ms = now_ms() - t0;
        double ns_per_call = (elapsed_ms * 1000000.0) / (double)cases[i].iterations;

        uint64_t pixels = (uint64_t)cases[i].width * (uint64_t)cases[i].height;
        if (last_alloc.feasible) {
            feasible_count++;
            if (pixels > max_feasible_pixels) {
                max_feasible_pixels = pixels;
                max_feasible_case = cases[i].name;
            }
        }

        printf("%-20s %4dx%-4d frames=%-4d budget=%-4" PRIu64 "MB "
               "feas=%d thr=%-2d batch=%-4d ppm=%" PRIu64 "B %.2fms (%.1fns/op)\n",
               cases[i].name, cases[i].width, cases[i].height,
               cases[i].total_frames, cases[i].budget_mb,
               last_alloc.feasible, last_alloc.threads, last_alloc.batch_frames,
               ppm_bytes, elapsed_ms, ns_per_call);

        fprintf(logf,
                "{\"case\":\"%s\",\"w\":%d,\"h\":%d,\"frames\":%d,"
                "\"budget_mb\":%" PRIu64 ",\"feasible\":%d,\"threads\":%d,"
                "\"batch_frames\":%d,\"ppm_bytes\":%" PRIu64 ","
                "\"iters\":%d,\"elapsed_ms\":%.3f,\"ns_per_op\":%.2f,\"temp_c\":%.1f}\n",
                cases[i].name, cases[i].width, cases[i].height,
                cases[i].total_frames, cases[i].budget_mb,
                last_alloc.feasible, last_alloc.threads, last_alloc.batch_frames,
                ppm_bytes, cases[i].iterations, elapsed_ms, ns_per_call,
                t_valid ? t_now : -1.0);
    }

    fclose(logf);

    double temp_end = -1.0;
    int temp_end_ok = read_cpu_temp_c(&temp_end);
    printf("[STRESS SUMMARY] feasible_cases=%d/%zu max_feasible_case=%s "
           "temp_start=%.1fC temp_end=%.1fC log=%s\n",
           feasible_count, num_cases, max_feasible_case,
           temp_ok ? temp_start : -1.0,
           temp_end_ok ? temp_end : -1.0,
           log_path);
    return 0;
}
C_SRC

cc -std=c11 -O3 -Wall -Wextra tools/pi_stress.c tools/pi_lib.c -lm -o tools/pi_stress
./tools/pi_stress

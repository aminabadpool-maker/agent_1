#define _POSIX_C_SOURCE 200809L
#include "pi_lib.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

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

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [width height total_frames budget_mb max_threads max_batch min_ram_mb min_disk_mb max_temp_c watch_dir]\n",
            prog);
}

int main(int argc, char **argv) {
    int width = (argc > 1) ? atoi(argv[1]) : 1280;
    int height = (argc > 2) ? atoi(argv[2]) : 720;
    int total_frames = (argc > 3) ? atoi(argv[3]) : 300;
    uint64_t budget_mb = (argc > 4) ? (uint64_t)strtoull(argv[4], NULL, 10) : 500ULL;
    int max_threads_cap = (argc > 5) ? atoi(argv[5]) : 4;
    int max_safe_batch = (argc > 6) ? atoi(argv[6]) : 128;
    uint64_t min_ram_mb = (argc > 7) ? (uint64_t)strtoull(argv[7], NULL, 10) : 512ULL;
    long min_disk_mb = (argc > 8) ? atol(argv[8]) : 1024L;
    double max_temp_c = (argc > 9) ? atof(argv[9]) : 75.0;
    const char *watch_dir = (argc > 10) ? argv[10] : "renders/pi_run";

    if (width <= 0 || height <= 0 || total_frames <= 0 ||
        budget_mb == 0ULL || max_threads_cap <= 0 || max_safe_batch <= 0) {
        print_usage(argv[0]);
        return 2;
    }

    PiCaps caps = pi_probe_system();
    long disk_free_mb = read_disk_free_mb(watch_dir);
    double temp_c = -1.0;
    int temp_ok_read = read_cpu_temp_c(&temp_c);

    PiPolicy policy;
    policy.width = width;
    policy.height = height;
    policy.total_frames = total_frames;
    policy.ram_budget_mb = budget_mb;
    policy.max_threads_cap = max_threads_cap;

    PiAllocation alloc = pi_plan_dynamic(caps, policy);

    int ok = 1;
    if (caps.avail_ram_mb < min_ram_mb) {
        fprintf(stderr, "[GUARD BLOCK] Low RAM: %" PRIu64 "MB < min %" PRIu64 "MB\n",
                caps.avail_ram_mb, min_ram_mb);
        ok = 0;
    }
    if (disk_free_mb < min_disk_mb) {
        fprintf(stderr, "[GUARD BLOCK] Low Disk on '%s': %ldMB < min %ldMB\n",
                watch_dir, disk_free_mb, min_disk_mb);
        ok = 0;
    }
    if (temp_ok_read && temp_c > max_temp_c) {
        fprintf(stderr, "[GUARD BLOCK] High CPU Temp: %.1fC > max %.1fC\n",
                temp_c, max_temp_c);
        ok = 0;
    }
    if (!alloc.feasible) {
        fprintf(stderr, "[GUARD BLOCK] Allocation marked infeasible by pi_plan_dynamic\n");
        ok = 0;
    }
    if (alloc.threads > max_threads_cap) {
        fprintf(stderr, "[GUARD BLOCK] Threads %d exceed cap %d\n",
                alloc.threads, max_threads_cap);
        ok = 0;
    }
    if (alloc.batch_frames > max_safe_batch) {
        fprintf(stderr, "[GUARD BLOCK] Batch frames %d exceed safe limit %d\n",
                alloc.batch_frames, max_safe_batch);
        ok = 0;
    }

    if (!ok) {
        return 1;
    }

    printf("[GUARD PASS] ram=%" PRIu64 "MB disk=%ldMB temp=%.1fC threads=%d batch=%d\n",
           caps.avail_ram_mb, disk_free_mb, temp_ok_read ? temp_c : -1.0,
           alloc.threads, alloc.batch_frames);
    return 0;
}

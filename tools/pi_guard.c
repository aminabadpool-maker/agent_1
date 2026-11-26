#include <stdio.h>
#include <stdlib.h>
#include "safe_batch.h"

static uint64_t read_avail_ram_mb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64];
    uint64_t kb = 0, avail_kb = 0;
    while (fscanf(f, "%63s %llu kB%*[^\n]", key, (unsigned long long *)&kb) == 2) {
        if (key[0] == 'M' && key[1] == 'e' && key[2] == 'm' &&
            key[3] == 'A' && key[4] == 'v' && key[5] == 'a' && key[6] == 'i' && key[7] == 'l') {
            avail_kb = kb;
            break;
        }
    }
    fclose(f);
    return avail_kb / 1024ULL;
}

#include <sys/statvfs.h>
static uint64_t read_free_disk_mb(const char *path) {
    struct statvfs st;
    if (!path || statvfs(path, &st) != 0) return 0;
    uint64_t free_bytes = (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
    return free_bytes / (1024ULL * 1024ULL);
}

static double read_temp_c(void) {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return 37.0;
    long mdeg = 0;
    int ok = fscanf(f, "%ld", &mdeg);
    fclose(f);
    if (ok != 1) return 37.0;
    return (double)mdeg / 1000.0;
}

int main(int argc, char **argv) {
    if (argc < 10) {
        fprintf(stderr, "usage: pi_guard W H FRAMES BUDGET_MB MAX_THREADS MAX_BATCH MIN_DISK_MB MAX_DISK_MB MAX_TEMP_C OUT_DIR [QUEUED_JOBS]\n");
        return 2;
    }
    safe_batch_cfg_t cfg = {
        .width = atoi(argv[1]),
        .height = atoi(argv[2]),
        .frames = atoi(argv[3]),
        .budget_mb = atoi(argv[4]),
        .max_threads = atoi(argv[5]),
        .max_batch = atoi(argv[6]),
        .min_free_disk_mb = atoi(argv[7]),
        .queued_jobs = (argc > 11 && atoi(argv[11]) > 0) ? atoi(argv[11]) : 1,
        .temp_c = read_temp_c(),
        .max_temp_c = atof(argv[9])
    };
    uint64_t ram_mb = read_avail_ram_mb();
    uint64_t disk_mb = read_free_disk_mb(argv[10]);
    int batch = 0;
    int rc = compute_safe_batch(&cfg, ram_mb, disk_mb, &batch);
    if (rc != 0) {
        fprintf(stderr, "[GUARD BLOCK] rc=%d ram=%lluMB disk=%lluMB temp=%.1fC\n",
                rc, (unsigned long long)ram_mb, (unsigned long long)disk_mb, cfg.temp_c);
        return 1;
    }
    printf("[GUARD PASS] ram=%lluMB disk=%lluMB temp=%.1fC threads=%d batch=%d\n",
           (unsigned long long)ram_mb, (unsigned long long)disk_mb, cfg.temp_c, cfg.max_threads, batch);
    return 0;
}

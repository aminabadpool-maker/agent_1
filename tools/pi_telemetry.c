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
    long disk_free_mb;
    double cpu_temp_c;
    int cpu_temp_ok;
    int disk_ok;
    int healthy;
} node_health_t;

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

static void ensure_parent_dir(const char *filepath) {
    if (!filepath) {
        return;
    }
    char buf[512];
    size_t len = strlen(filepath);
    if (len == 0 || len >= sizeof(buf)) {
        return;
    }
    memcpy(buf, filepath, len + 1);
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf) {
        return;
    }
    *slash = '\0';
    mkdir(buf, 0755);
}

static void format_utc_now(char *out, size_t cap) {
    time_t now = time(NULL);
    struct tm tm_utc;
    if (gmtime_r(&now, &tm_utc)) {
        strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    } else {
        snprintf(out, cap, "1970-01-01T00:00:00Z");
    }
}

static void collect_health(const char *watch_dir, long min_disk_mb,
                           double max_temp_c, node_health_t *out_h) {
    memset(out_h, 0, sizeof(*out_h));
    out_h->disk_free_mb = read_disk_free_mb(watch_dir);
    out_h->cpu_temp_ok = read_cpu_temp_c(&out_h->cpu_temp_c);

    out_h->disk_ok = (out_h->disk_free_mb >= min_disk_mb) ? 1 : 0;
    int temp_ok = (!out_h->cpu_temp_ok || out_h->cpu_temp_c <= max_temp_c) ? 1 : 0;
    out_h->healthy = (out_h->disk_ok && temp_ok) ? 1 : 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [width height total_frames budget_mb max_threads watch_dir log_file]\n",
            prog);
}

int main(int argc, char **argv) {
    int width = (argc > 1) ? atoi(argv[1]) : 1280;
    int height = (argc > 2) ? atoi(argv[2]) : 720;
    int total_frames = (argc > 3) ? atoi(argv[3]) : 300;
    uint64_t budget_mb = (argc > 4) ? (uint64_t)strtoull(argv[4], NULL, 10) : 500ULL;
    int max_threads_cap = (argc > 5) ? atoi(argv[5]) : 0;
    const char *watch_dir = (argc > 6) ? argv[6] : "renders/pi_run";
    const char *log_file = (argc > 7) ? argv[7] : "renders/pi_run/telemetry.jsonl";

    if (width <= 0 || height <= 0 || total_frames <= 0 || budget_mb == 0ULL) {
        print_usage(argv[0]);
        return 2;
    }

    PiCaps caps = pi_probe_system();
    node_health_t health;
    collect_health(watch_dir, 256, 80.0, &health);

    if (caps.avail_ram_mb == 0ULL) {
        caps.avail_ram_mb = 256ULL;
    }
    if (caps.cpu_cores <= 0) {
        caps.cpu_cores = 1;
    }

    PiPolicy policy;
    policy.width = width;
    policy.height = height;
    policy.total_frames = total_frames;
    policy.ram_budget_mb = budget_mb;
    policy.max_threads_cap = max_threads_cap;

    PiAllocation alloc = pi_plan_dynamic(caps, policy);
    uint64_t est_ppm = pi_estimate_ppm_bytes(width, height);

    char ts[32];
    format_utc_now(ts, sizeof(ts));

    ensure_parent_dir(log_file);
    FILE *out = fopen(log_file, "a");
    if (!out) {
        fprintf(stderr, "[ERR] Cannot open log file '%s': %s\n",
                log_file, strerror(errno));
        return 1;
    }

    fprintf(out,
            "{\"ts\":\"%s\",\"healthy\":%d,\"cores\":%ld,\"avail_ram_mb\":%" PRIu64
            ",\"disk_free_mb\":%ld,\"temp_c\":%.1f,\"temp_valid\":%d,"
            "\"res\":\"%dx%d\",\"frames\":%d,\"budget_mb\":%" PRIu64
            ",\"feasible\":%d,\"threads\":%d,\"batch_frames\":%d,"
            "\"frame_bytes\":%" PRIu64 ",\"est_ppm_bytes\":%" PRIu64
            ",\"budget_bytes\":%" PRIu64 "}\n",
            ts, health.healthy, caps.cpu_cores, caps.avail_ram_mb,
            health.disk_free_mb, health.cpu_temp_ok ? health.cpu_temp_c : -1.0,
            health.cpu_temp_ok, width, height, total_frames, budget_mb,
            alloc.feasible, alloc.threads, alloc.batch_frames,
            alloc.frame_bytes, est_ppm, alloc.budget_bytes);
    fclose(out);

    printf("ts=%s healthy=%d cores=%ld avail_ram_mb=%" PRIu64
           " disk_free_mb=%ld temp_c=%.1f feasible=%d threads=%d"
           " batch_frames=%d log=%s\n",
           ts, health.healthy, caps.cpu_cores, caps.avail_ram_mb,
           health.disk_free_mb, health.cpu_temp_ok ? health.cpu_temp_c : -1.0,
           alloc.feasible, alloc.threads, alloc.batch_frames, log_file);

    return health.healthy ? 0 : 1;
}

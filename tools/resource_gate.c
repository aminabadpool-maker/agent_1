#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

typedef struct {
    uint64_t min_free_ram_mb;
    uint64_t min_free_disk_mb;
    uint64_t max_temp_mc;      /* millidegrees Celsius, e.g. 80000 = 80.0 C */
    uint64_t est_job_ram_mb;
    bool dry_run;
} GateConfig;

typedef struct {
    uint64_t avail_ram_mb;
    bool ram_known;
    uint64_t free_disk_mb;
    bool disk_known;
    uint64_t temp_mc;
    bool temp_known;
} SystemSnapshot;

static uint64_t read_meminfo_avail_mb(const char *meminfo_path, bool *ok) {
    if (ok) *ok = false;
    FILE *fp = fopen(meminfo_path ? meminfo_path : "/proc/meminfo", "r");
    if (!fp) return 0;

    char line[256];
    uint64_t avail_kb = 0;
    uint64_t free_kb = 0;
    bool found_avail = false;
    bool found_free = false;

    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long long val_kb = 0;
        if (sscanf(line, "MemAvailable: %llu kB", &val_kb) == 1) {
            avail_kb = (uint64_t)val_kb;
            found_avail = true;
            break;
        }
        if (sscanf(line, "MemFree: %llu kB", &val_kb) == 1) {
            free_kb = (uint64_t)val_kb;
            found_free = true;
        }
    }
    fclose(fp);

    if (found_avail) {
        if (ok) *ok = true;
        return avail_kb / 1024u;
    }
    if (found_free) {
        if (ok) *ok = true;
        return free_kb / 1024u;
    }
    return 0;
}

static uint64_t read_free_disk_mb(const char *target_path, bool *ok) {
    if (ok) *ok = false;
    if (!target_path || target_path[0] == '\0') return 0;

    struct statvfs vfs;
    if (statvfs(target_path, &vfs) != 0) return 0;

    uint64_t bavail = (uint64_t)vfs.f_bavail;
    uint64_t frsize = (uint64_t)vfs.f_frsize;
    if (frsize == 0) frsize = (uint64_t)vfs.f_bsize;
    if (frsize == 0 || bavail > UINT64_MAX / frsize) return 0;

    uint64_t bytes_free = bavail * frsize;
    if (ok) *ok = true;
    return bytes_free / (1024u * 1024u);
}

static uint64_t read_thermal_mc(const char *thermal_path, bool *ok) {
    if (ok) *ok = false;
    FILE *fp = fopen(thermal_path ? thermal_path : "/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) return 0;

    long long raw = 0;
    int scanned = fscanf(fp, "%lld", &raw);
    fclose(fp);

    if (scanned != 1 || raw < 0) return 0;
    if (ok) *ok = true;
    return (uint64_t)raw;
}

static int evaluate_gate(const GateConfig *cfg, const SystemSnapshot *snap, char *reason, size_t rlen) {
    if (!cfg || !snap || !reason || rlen == 0) return 2;

    if (cfg->est_job_ram_mb > 0 && cfg->min_free_ram_mb > UINT64_MAX - cfg->est_job_ram_mb) {
        snprintf(reason, rlen, "ram_threshold_overflow");
        return 2;
    }

    uint64_t needed_ram_mb = cfg->min_free_ram_mb + cfg->est_job_ram_mb;
    if (snap->ram_known && snap->avail_ram_mb < needed_ram_mb) {
        snprintf(reason, rlen, "insufficient_ram(avail=%" PRIu64 "MB, needed=%" PRIu64 "MB)",
                 snap->avail_ram_mb, needed_ram_mb);
        return 1;
    }

    if (snap->disk_known && snap->free_disk_mb < cfg->min_free_disk_mb) {
        snprintf(reason, rlen, "insufficient_disk(free=%" PRIu64 "MB, min=%" PRIu64 "MB)",
                 snap->free_disk_mb, cfg->min_free_disk_mb);
        return 1;
    }

    if (snap->temp_known && cfg->max_temp_mc > 0 && snap->temp_mc > cfg->max_temp_mc) {
        snprintf(reason, rlen, "thermal_limit_exceeded(temp_mc=%" PRIu64 ", max_mc=%" PRIu64 ")",
                 snap->temp_mc, cfg->max_temp_mc);
        return 1;
    }

    snprintf(reason, rlen, "ok(avail_ram=%" PRIu64 "MB, free_disk=%" PRIu64 "MB, temp_mc=%" PRIu64 ")",
             snap->ram_known ? snap->avail_ram_mb : 0u,
             snap->disk_known ? snap->free_disk_mb : 0u,
             snap->temp_known ? snap->temp_mc : 0u);
    return 0;
}

static int parse_u64_arg(const char *s, uint64_t *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static int run_self_tests(void) {
    GateConfig cfg = {
        .min_free_ram_mb = 256,
        .min_free_disk_mb = 512,
        .max_temp_mc = 80000,
        .est_job_ram_mb = 128,
        .dry_run = true
    };
    SystemSnapshot snap_ok = {
        .avail_ram_mb = 1024, .ram_known = true,
        .free_disk_mb = 2048, .disk_known = true,
        .temp_mc = 52000,   .temp_known = true
    };
    SystemSnapshot snap_hot = {
        .avail_ram_mb = 1024, .ram_known = true,
        .free_disk_mb = 2048, .disk_known = true,
        .temp_mc = 88000,   .temp_known = true
    };

    char reason[256];
    if (evaluate_gate(&cfg, &snap_ok, reason, sizeof(reason)) != 0) {
        fprintf(stderr, "[FAIL] Expected healthy snapshot to pass, got: %s\n", reason);
        return 1;
    }
    if (evaluate_gate(&cfg, &snap_hot, reason, sizeof(reason)) != 1) {
        fprintf(stderr, "[FAIL] Expected over-temp snapshot to block, got: %s\n", reason);
        return 1;
    }

    printf("[SELF-TEST PASS] resource_gate evaluation checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    /* Usage: resource_gate <target_dir> <min_free_ram_mb> <est_job_ram_mb> <min_free_disk_mb> <max_temp_mc> [--dry-run] */
    if (argc < 6 || argc > 7) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <target_dir> <min_free_ram_mb> <est_job_ram_mb> <min_free_disk_mb> <max_temp_mc> [--dry-run]\n",
                argv[0], argv[0]);
        return 2;
    }

    GateConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (parse_u64_arg(argv[2], &cfg.min_free_ram_mb) != 0 ||
        parse_u64_arg(argv[3], &cfg.est_job_ram_mb) != 0 ||
        parse_u64_arg(argv[4], &cfg.min_free_disk_mb) != 0 ||
        parse_u64_arg(argv[5], &cfg.max_temp_mc) != 0) {
        fprintf(stderr, "[BLOCK] Invalid numeric threshold argument.\n");
        return 2;
    }

    if (argc == 7) {
        if (strcmp(argv[6], "--dry-run") == 0) {
            cfg.dry_run = true;
        } else {
            fprintf(stderr, "[BLOCK] Unknown flag: %s\n", argv[6]);
            return 2;
        }
    }

    SystemSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.avail_ram_mb = read_meminfo_avail_mb("/proc/meminfo", &snap.ram_known);
    snap.free_disk_mb = read_free_disk_mb(argv[1], &snap.disk_known);
    snap.temp_mc = read_thermal_mc("/sys/class/thermal/thermal_zone0/temp", &snap.temp_known);

    char reason[256];
    int rc = evaluate_gate(&cfg, &snap, reason, sizeof(reason));
    printf("[%s%s] %s\n",
           cfg.dry_run ? "DRY-RUN " : "",
           rc == 0 ? "GATE PASS" : (rc == 1 ? "GATE BLOCK" : "GATE ERROR"),
           reason);
    return rc;
}

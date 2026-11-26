#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "tools/safe_batch.h"

static int g_pass = 0;
static int g_fail = 0;

static void check_rc(const char *name, const safe_batch_cfg_t *cfg,
                     uint64_t ram_mb, uint64_t disk_mb,
                     int exp_rc, int exp_batch) {
    int batch = -999;
    int rc = compute_safe_batch(cfg, ram_mb, disk_mb, &batch);
    if (rc != exp_rc) {
        fprintf(stderr, "[FAIL] %s: expected rc=%d, got rc=%d (batch=%d)\n",
                name, exp_rc, rc, batch);
        g_fail++;
        return;
    }
    if (exp_rc == 0 && batch != exp_batch) {
        fprintf(stderr, "[FAIL] %s: expected batch=%d, got batch=%d\n",
                name, exp_batch, batch);
        g_fail++;
        return;
    }
    printf("[OK]   %s -> rc=%d batch=%d\n", name, rc, batch);
    g_pass++;
}

static safe_batch_cfg_t base_4k_cfg(int queued_jobs, int min_disk_mb) {
    safe_batch_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.width = 3840;
    cfg.height = 2160;
    cfg.frames = 300;
    cfg.budget_mb = 2048;
    cfg.max_threads = 8;
    cfg.max_batch = 256;
    cfg.min_free_disk_mb = min_disk_mb;
    cfg.queued_jobs = queued_jobs;
    cfg.temp_c = 42.0;
    cfg.max_temp_c = 75.0;
    return cfg;
}

static void test_input_and_thermal_limits(void) {
    safe_batch_cfg_t cfg = base_4k_cfg(1, 512);
    int b = 0;

    if (compute_safe_batch(NULL, 4096, 50000, &b) != -1) {
        fprintf(stderr, "[FAIL] null cfg should return -1\n");
        g_fail++;
    } else {
        printf("[OK]   null cfg -> rc=-1\n");
        g_pass++;
    }

    cfg = base_4k_cfg(1, 512);
    cfg.width = 0;
    check_rc("zero width", &cfg, 4096, 50000, -1, 0);

    cfg = base_4k_cfg(1, 512);
    cfg.queued_jobs = 0;
    check_rc("zero queued_jobs", &cfg, 4096, 50000, -1, 0);

    cfg = base_4k_cfg(1, 512);
    cfg.temp_c = 75.0;
    check_rc("temp equal to max_temp_c", &cfg, 4096, 50000, -1, 0);

    cfg = base_4k_cfg(1, 512);
    cfg.temp_c = 81.5;
    cfg.max_temp_c = 75.0;
    check_rc("overheat temp", &cfg, 4096, 50000, -1, 0);

    cfg = base_4k_cfg(1, 512);
    cfg.temp_c = -25.0;
    check_rc("invalid low temp", &cfg, 4096, 50000, -1, 0);
}

static void test_disk_queue_constraints(void) {
    safe_batch_cfg_t cfg = base_4k_cfg(20, 512);
    check_rc("20 jobs x 512MB = 10240MB pass", &cfg, 3900, 46256, 0, 64);

    cfg = base_4k_cfg(20, 2313);
    check_rc("20 jobs x 2313MB = 46260MB > 46256MB block", &cfg, 3900, 46256, -2, 0);

    cfg = base_4k_cfg(20, 3000);
    check_rc("20 jobs x 3000MB = 60000MB block", &cfg, 3900, 46256, -2, 0);
}

static void test_batch_cap_and_ram(void) {
    safe_batch_cfg_t cfg = base_4k_cfg(1, 256);
    cfg.max_batch = 512;
    cfg.budget_mb = 8192;
    check_rc("4K hard cap at 64", &cfg, 8192, 50000, 0, 64);

    cfg = base_4k_cfg(1, 256);
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.frames = 500;
    cfg.budget_mb = 1024;
    cfg.max_batch = 128;
    check_rc("1080p capped by max_batch=128", &cfg, 4096, 50000, 0, 128);

    cfg = base_4k_cfg(1, 256);
    cfg.width = 7680;
    cfg.height = 4320;
    cfg.budget_mb = 16;
    check_rc("8K tiny budget -> rc=-4", &cfg, 4096, 50000, -4, 0);

    cfg = base_4k_cfg(1, 256);
    cfg.width = INT_MAX;
    cfg.height = INT_MAX;
    cfg.budget_mb = 1;
    check_rc("huge frame vs tiny budget -> rc=-4", &cfg, 4096, 50000, -4, 0);
}

static void test_overflow_guard(void) {
    /* Force uint64 overflow in budget byte conversion (eb * 1024 * 1024 > UINT64_MAX) */
    uint64_t huge_mb = (UINT64_MAX / (1024ULL * 1024ULL)) + 1ULL;
    safe_batch_cfg_t cfg = base_4k_cfg(1, 256);
    cfg.width = 1920;
    cfg.height = 1080;
    if (huge_mb <= (uint64_t)INT_MAX) {
        cfg.budget_mb = (int)huge_mb;
        check_rc("budget byte overflow -> rc=-3", &cfg, huge_mb, 50000, -3, 0);
    } else {
        /* If int cannot hold huge_mb, verify sb_mul_u64 detects u64 overflow directly */
        uint64_t out = 0;
        if (sb_mul_u64(huge_mb, 1024ULL * 1024ULL, &out)) {
            printf("[OK]   sb_mul_u64 u64 budget overflow -> detected\n");
            g_pass++;
        } else {
            fprintf(stderr, "[FAIL] sb_mul_u64 failed to detect u64 overflow\n");
            g_fail++;
        }
    }

    uint64_t px_ovf = 0;
    if (sb_mul_u64(UINT64_MAX / 2ULL + 1ULL, 3ULL, &px_ovf)) {
        printf("[OK]   sb_mul_u64 frame byte overflow -> detected\n");
        g_pass++;
    } else {
        fprintf(stderr, "[FAIL] sb_mul_u64 failed frame byte overflow\n");
        g_fail++;
    }
}

int main(void) {
    test_input_and_thermal_limits();
    test_disk_queue_constraints();
    test_batch_cap_and_ram();
    test_overflow_guard();

    printf("\nSUMMARY: passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

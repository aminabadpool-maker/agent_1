#ifndef AGENT1_SAFE_BATCH_H
#define AGENT1_SAFE_BATCH_H

#include <limits.h>
#include <stdint.h>

typedef struct {
    int width;
    int height;
    int frames;
    int budget_mb;
    int max_threads;
    int max_batch;
    int min_free_disk_mb;
    int queued_jobs;
    double temp_c;
    double max_temp_c;
} safe_batch_cfg_t;

static inline int sb_mul_u64(uint64_t a, uint64_t b, uint64_t *o) {
    if (!o) {
        return 1;
    }
    if (a != 0 && b > UINT64_MAX / a) {
        return 1;
    }
    *o = a * b;
    return 0;
}

static inline int compute_safe_batch(const safe_batch_cfg_t *c,
                                     uint64_t ram_mb,
                                     uint64_t disk_mb,
                                     int *out_batch) {
    if (!c || !out_batch) {
        return -1;
    }
    if (c->width <= 0 || c->height <= 0 || c->frames <= 0 ||
        c->budget_mb <= 0 || c->max_threads <= 0 || c->max_batch <= 0 ||
        c->queued_jobs <= 0 || c->min_free_disk_mb <= 0) {
        return -1;
    }
    if (c->temp_c < -20.0 || c->temp_c > 125.0 || !(c->temp_c < c->max_temp_c)) {
        return -1;
    }

    uint64_t needed_disk_mb = 0;
    if (sb_mul_u64((uint64_t)c->min_free_disk_mb, (uint64_t)c->queued_jobs, &needed_disk_mb)) {
        return -2;
    }
    if (disk_mb < needed_disk_mb) {
        return -2;
    }

    uint64_t px = 0, fb = 0, bb = 0;
    if (sb_mul_u64((uint64_t)c->width, (uint64_t)c->height, &px) ||
        sb_mul_u64(px, 3ULL, &fb) || fb == 0) {
        return -3;
    }

    uint64_t eb = ram_mb < (uint64_t)c->budget_mb ? ram_mb : (uint64_t)c->budget_mb;
    if (px >= 3840ULL * 2160ULL) {
        eb = (eb * 3ULL) / 4ULL;
    }
    if (sb_mul_u64(eb, 1024ULL * 1024ULL, &bb)) {
        return -3;
    }

    uint64_t m = bb / fb;
    if (m == 0) {
        return -4;
    }
    if (m > (uint64_t)c->frames) {
        m = (uint64_t)c->frames;
    }
    if (m > (uint64_t)c->max_batch) {
        m = (uint64_t)c->max_batch;
    }
    if (px >= 3840ULL * 2160ULL && m > 64ULL) {
        m = 64ULL;
    }
    if (m > (uint64_t)INT_MAX) {
        return -3;
    }

    *out_batch = (int)m;
    return 0;
}

#endif

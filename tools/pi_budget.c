#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    int width;
    int height;
    int total_frames;
    int avail_ram_mb;
    int cpu_cores;
    int max_ram_budget_mb;
} PiConfig;

typedef struct {
    int recommended_threads;
    int batch_frames;
    uint64_t single_ppm_bytes;
    uint64_t budget_bytes;
    int feasible;
} PiPlan;

static uint64_t calc_ppm_bytes(int w, int h) {
    return (uint64_t)w * (uint64_t)h * 3ULL + 64ULL;
}

static PiPlan plan_pi_render(PiConfig cfg) {
    PiPlan p;
    p.single_ppm_bytes = calc_ppm_bytes(cfg.width, cfg.height);
    p.budget_bytes = (uint64_t)cfg.max_ram_budget_mb * 1024ULL * 1024ULL;

    if (cfg.cpu_cores <= 1) {
        p.recommended_threads = 1;
    } else if (cfg.avail_ram_mb < 1024) {
        p.recommended_threads = (cfg.cpu_cores > 2) ? 2 : cfg.cpu_cores;
    } else {
        p.recommended_threads = (cfg.cpu_cores > 4) ? 4 : cfg.cpu_cores;
    }

    uint64_t max_frames_in_mem = p.single_ppm_bytes > 0
        ? (p.budget_bytes / p.single_ppm_bytes)
        : 1ULL;

    if (max_frames_in_mem == 0ULL) {
        max_frames_in_mem = 1ULL;
    }

    p.batch_frames = (int)max_frames_in_mem;
    if (p.batch_frames > cfg.total_frames && cfg.total_frames > 0) {
        p.batch_frames = cfg.total_frames;
    }

    p.feasible = (p.single_ppm_bytes <= p.budget_bytes) ? 1 : 0;
    return p;
}

int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <width> <height> <frames> <avail_ram_mb> <cpu_cores> <max_ram_budget_mb>\n", argv[0]);
        return 2;
    }

    PiConfig cfg;
    cfg.width = atoi(argv[1]);
    cfg.height = atoi(argv[2]);
    cfg.total_frames = atoi(argv[3]);
    cfg.avail_ram_mb = atoi(argv[4]);
    cfg.cpu_cores = atoi(argv[5]);
    cfg.max_ram_budget_mb = atoi(argv[6]);

    if (cfg.width <= 0 || cfg.height <= 0 || cfg.total_frames <= 0 ||
        cfg.avail_ram_mb <= 0 || cfg.cpu_cores <= 0 || cfg.max_ram_budget_mb <= 0) {
        fprintf(stderr, "[PI_BUDGET ERR] Invalid positive parameters\n");
        return 1;
    }

    PiPlan plan = plan_pi_render(cfg);
    printf("feasible=%d threads=%d batch_frames=%d ppm_bytes=%llu budget_bytes=%llu\n",
           plan.feasible,
           plan.recommended_threads,
           plan.batch_frames,
           (unsigned long long)plan.single_ppm_bytes,
           (unsigned long long)plan.budget_bytes);

    return plan.feasible ? 0 : 1;
}

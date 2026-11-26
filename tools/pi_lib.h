#ifndef AGENT1_PI_LIB_H
#define AGENT1_PI_LIB_H

#include <stdint.h>

typedef struct {
    long cpu_cores;
    uint64_t avail_ram_mb;
} PiCaps;

typedef struct {
    int width;
    int height;
    int total_frames;
    uint64_t ram_budget_mb;
    int max_threads_cap;
} PiPolicy;

typedef struct {
    int feasible;
    int threads;
    int batch_frames;
    uint64_t frame_bytes;
    uint64_t budget_bytes;
} PiAllocation;

PiCaps pi_probe_system(void);
uint64_t pi_estimate_ppm_bytes(int width, int height);
PiAllocation pi_plan_dynamic(PiCaps caps, PiPolicy policy);

#endif

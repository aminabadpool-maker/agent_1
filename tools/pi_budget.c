#include "pi_lib.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <width> <height> <frames> <avail_ram_mb> <cpu_cores> <max_ram_budget_mb>\n", argv[0]);
        return 2;
    }

    PiPolicy pol;
    pol.width = atoi(argv[1]);
    pol.height = atoi(argv[2]);
    pol.total_frames = atoi(argv[3]);

    PiCaps caps;
    caps.avail_ram_mb = (uint64_t)strtoull(argv[4], NULL, 10);
    caps.cpu_cores = atol(argv[5]);

    pol.ram_budget_mb = (uint64_t)strtoull(argv[6], NULL, 10);
    pol.max_threads_cap = 0;

    if (pol.width <= 0 || pol.height <= 0 || pol.total_frames <= 0 ||
        caps.avail_ram_mb == 0ULL || caps.cpu_cores <= 0L || pol.ram_budget_mb == 0ULL) {
        fprintf(stderr, "[PI_BUDGET ERR] Invalid positive parameters\n");
        return 1;
    }

    PiAllocation plan = pi_plan_dynamic(caps, pol);
    printf("feasible=%d threads=%d batch_frames=%d ppm_bytes=%llu budget_bytes=%llu\n",
           plan.feasible,
           plan.threads,
           plan.batch_frames,
           (unsigned long long)plan.frame_bytes,
           (unsigned long long)plan.budget_bytes);

    return plan.feasible ? 0 : 1;
}

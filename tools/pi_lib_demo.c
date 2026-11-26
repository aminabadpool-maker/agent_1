#include "pi_lib.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    PiCaps caps = pi_probe_system();
    if (argc >= 3) {
        caps.avail_ram_mb = (uint64_t)strtoull(argv[1], NULL, 10);
        caps.cpu_cores = atol(argv[2]);
    }

    PiPolicy pol;
    pol.width = (argc >= 4) ? atoi(argv[3]) : 1280;
    pol.height = (argc >= 5) ? atoi(argv[4]) : 720;
    pol.total_frames = (argc >= 6) ? atoi(argv[5]) : 500;
    pol.ram_budget_mb = (argc >= 7) ? (uint64_t)strtoull(argv[6], NULL, 10) : 0ULL;
    pol.max_threads_cap = (argc >= 8) ? atoi(argv[7]) : 0;

    PiAllocation a = pi_plan_dynamic(caps, pol);
    printf("cores=%ld avail_ram_mb=%llu feasible=%d threads=%d batch_frames=%d frame_bytes=%llu budget_bytes=%llu\n",
           caps.cpu_cores,
           (unsigned long long)caps.avail_ram_mb,
           a.feasible,
           a.threads,
           a.batch_frames,
           (unsigned long long)a.frame_bytes,
           (unsigned long long)a.budget_bytes);

    return a.feasible ? 0 : 1;
}

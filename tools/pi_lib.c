#define _POSIX_C_SOURCE 200809L
#include "pi_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

PiCaps pi_probe_system(void) {
    PiCaps caps;
    caps.cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (caps.cpu_cores < 1) {
        caps.cpu_cores = 1;
    }

    caps.avail_ram_mb = 0ULL;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            unsigned long long kb = 0ULL;
            if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
                caps.avail_ram_mb = (uint64_t)(kb / 1024ULL);
                break;
            }
        }
        fclose(fp);
    }

    if (caps.avail_ram_mb == 0ULL) {
        caps.avail_ram_mb = 512ULL;
    }

    return caps;
}

uint64_t pi_estimate_ppm_bytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0ULL;
    }
    return (uint64_t)width * (uint64_t)height * 3ULL + 64ULL;
}

PiAllocation pi_plan_dynamic(PiCaps caps, PiPolicy policy) {
    PiAllocation out;
    memset(&out, 0, sizeof(out));

    if (policy.width <= 0 || policy.height <= 0 || policy.total_frames <= 0) {
        return out;
    }

    if (caps.cpu_cores < 1) {
        caps.cpu_cores = 1;
    }
    if (caps.avail_ram_mb == 0ULL) {
        caps.avail_ram_mb = 512ULL;
    }

    uint64_t effective_mb = policy.ram_budget_mb;
    if (effective_mb == 0ULL) {
        effective_mb = caps.avail_ram_mb / 2ULL;
        if (effective_mb == 0ULL) {
            effective_mb = 1ULL;
        }
    }

    if (effective_mb > caps.avail_ram_mb) {
        effective_mb = caps.avail_ram_mb;
    }

    out.frame_bytes = pi_estimate_ppm_bytes(policy.width, policy.height);
    out.budget_bytes = effective_mb * 1024ULL * 1024ULL;

    if (out.frame_bytes == 0ULL || out.budget_bytes < out.frame_bytes) {
        out.feasible = 0;
        out.threads = 1;
        out.batch_frames = 0;
        return out;
    }

    out.feasible = 1;

    uint64_t max_by_ram = out.budget_bytes / out.frame_bytes;
    if (max_by_ram < 1ULL) {
        max_by_ram = 1ULL;
    }
    if (max_by_ram > (uint64_t)policy.total_frames) {
        max_by_ram = (uint64_t)policy.total_frames;
    }
    out.batch_frames = (int)max_by_ram;

    long t = caps.cpu_cores;
    if ((long)out.batch_frames < t) {
        t = (long)out.batch_frames;
    }
    if (policy.max_threads_cap > 0 && t > (long)policy.max_threads_cap) {
        t = (long)policy.max_threads_cap;
    }
    if (t < 1L) {
        t = 1L;
    }
    out.threads = (int)t;

    return out;
}

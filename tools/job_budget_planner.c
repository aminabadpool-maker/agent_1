#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t requested_jobs;
    uint64_t max_jobs;
    uint64_t total_budget_mb;
    uint64_t per_job_ram_mb;
    uint64_t max_total_sec;
    uint64_t per_job_sec;
} BudgetInput;

typedef struct {
    uint64_t approved_jobs;
    uint64_t total_planned_ram_mb;
    uint64_t total_planned_sec;
    bool clamped_by_max_jobs;
    bool clamped_by_ram;
    bool clamped_by_time;
    char status[64];
    char detail[256];
} BudgetPlan;

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long val = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)val;
    return 0;
}

static int plan_batch_budget(const BudgetInput *in, BudgetPlan *out) {
    if (!in || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (in->requested_jobs == 0 || in->max_jobs == 0 ||
        in->total_budget_mb == 0 || in->per_job_ram_mb == 0 ||
        in->max_total_sec == 0 || in->per_job_sec == 0) {
        snprintf(out->status, sizeof(out->status), "REJECTED");
        snprintf(out->detail, sizeof(out->detail), "all budget parameters must be greater than zero");
        return 1;
    }

    if (in->per_job_ram_mb > in->total_budget_mb) {
        snprintf(out->status, sizeof(out->status), "REJECTED");
        snprintf(out->detail, sizeof(out->detail),
                 "single job ram (%" PRIu64 "MB) exceeds total budget (%" PRIu64 "MB)",
                 in->per_job_ram_mb, in->total_budget_mb);
        return 1;
    }

    if (in->per_job_sec > in->max_total_sec) {
        snprintf(out->status, sizeof(out->status), "REJECTED");
        snprintf(out->detail, sizeof(out->detail),
                 "single job duration (%" PRIu64 "s) exceeds max window (%" PRIu64 "s)",
                 in->per_job_sec, in->max_total_sec);
        return 1;
    }

    uint64_t jobs = in->requested_jobs;
    if (jobs > in->max_jobs) {
        jobs = in->max_jobs;
        out->clamped_by_max_jobs = true;
    }

    uint64_t ram_cap_jobs = in->total_budget_mb / in->per_job_ram_mb;
    if (jobs > ram_cap_jobs) {
        jobs = ram_cap_jobs;
        out->clamped_by_ram = true;
    }

    uint64_t time_cap_jobs = in->max_total_sec / in->per_job_sec;
    if (jobs > time_cap_jobs) {
        jobs = time_cap_jobs;
        out->clamped_by_time = true;
    }

    if (jobs == 0) {
        snprintf(out->status, sizeof(out->status), "REJECTED");
        snprintf(out->detail, sizeof(out->detail), "zero jobs fit within safety limits");
        return 1;
    }

    out->approved_jobs = jobs;
    out->total_planned_ram_mb = jobs * in->per_job_ram_mb;
    out->total_planned_sec = jobs * in->per_job_sec;

    if (out->clamped_by_max_jobs || out->clamped_by_ram || out->clamped_by_time) {
        snprintf(out->status, sizeof(out->status), "CLAMPED");
        snprintf(out->detail, sizeof(out->detail),
                 "approved=%" PRIu64 "/%" PRIu64 " jobs, ram=%" PRIu64 "MB, est_sec=%" PRIu64 "s",
                 out->approved_jobs, in->requested_jobs,
                 out->total_planned_ram_mb, out->total_planned_sec);
    } else {
        snprintf(out->status, sizeof(out->status), "APPROVED");
        snprintf(out->detail, sizeof(out->detail),
                 "approved=%" PRIu64 " jobs, ram=%" PRIu64 "MB, est_sec=%" PRIu64 "s",
                 out->approved_jobs, out->total_planned_ram_mb, out->total_planned_sec);
    }

    return 0;
}

static int run_self_tests(void) {
    BudgetInput clean_in = {
        .requested_jobs = 4,
        .max_jobs = 16,
        .total_budget_mb = 1024,
        .per_job_ram_mb = 128,
        .max_total_sec = 600,
        .per_job_sec = 60
    };
    BudgetPlan plan;

    if (plan_batch_budget(&clean_in, &plan) != 0 || strcmp(plan.status, "APPROVED") != 0 ||
        plan.approved_jobs != 4) {
        fprintf(stderr, "[FAIL] Expected clean batch to be APPROVED for 4 jobs\n");
        return 1;
    }

    BudgetInput clamped_in = {
        .requested_jobs = 50,
        .max_jobs = 32,
        .total_budget_mb = 1024,
        .per_job_ram_mb = 256, /* max 4 jobs by RAM */
        .max_total_sec = 600,
        .per_job_sec = 60
    };
    if (plan_batch_budget(&clamped_in, &plan) != 0 || strcmp(plan.status, "CLAMPED") != 0 ||
        plan.approved_jobs != 4 || !plan.clamped_by_ram) {
        fprintf(stderr, "[FAIL] Expected batch to clamp to 4 jobs by RAM\n");
        return 1;
    }

    BudgetInput rejected_in = {
        .requested_jobs = 2,
        .max_jobs = 10,
        .total_budget_mb = 256,
        .per_job_ram_mb = 512,
        .max_total_sec = 600,
        .per_job_sec = 60
    };
    if (plan_batch_budget(&rejected_in, &plan) != 1 || strcmp(plan.status, "REJECTED") != 0) {
        fprintf(stderr, "[FAIL] Expected oversized single job to be REJECTED\n");
        return 1;
    }

    printf("[SELF-TEST PASS] job_budget_planner checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    /* Usage: job_budget_planner <req_jobs> <max_jobs> <budget_mb> <job_ram_mb> <max_sec> <job_sec> */
    if (argc != 7) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <req_jobs> <max_jobs> <budget_mb> <job_ram_mb> <max_sec> <job_sec>\n",
                argv[0], argv[0]);
        return 2;
    }

    BudgetInput in;
    if (parse_u64(argv[1], &in.requested_jobs) != 0 ||
        parse_u64(argv[2], &in.max_jobs) != 0 ||
        parse_u64(argv[3], &in.total_budget_mb) != 0 ||
        parse_u64(argv[4], &in.per_job_ram_mb) != 0 ||
        parse_u64(argv[5], &in.max_total_sec) != 0 ||
        parse_u64(argv[6], &in.per_job_sec) != 0) {
        fprintf(stderr, "[REJECTED] Invalid unsigned integer parameter.\n");
        return 2;
    }

    BudgetPlan plan;
    int rc = plan_batch_budget(&in, &plan);
    printf("[%s] %s\n", plan.status, plan.detail);
    return rc;
}

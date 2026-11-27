#ifndef AGENT1_RENDER_QUEUE_H
#define AGENT1_RENDER_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#define RQ_MAX_JOBS 1024U

typedef enum {
    RQ_OK = 0,
    RQ_ERR_INVALID = -1,
    RQ_ERR_FULL = -2,
    RQ_ERR_BUDGET = -3,
    RQ_ERR_DUPLICATE = -4,
    RQ_ERR_NOT_FOUND = -5,
    RQ_ERR_STATE = -6
} RqStatus;

typedef enum {
    RQ_JOB_QUEUED = 0,
    RQ_JOB_ACTIVE = 1,
    RQ_JOB_DONE = 2,
    RQ_JOB_FAILED = 3
} RqJobState;

typedef struct {
    uint64_t job_id;
    uint32_t width;
    uint32_t height;
    uint64_t est_mb;
    RqJobState state;
    int in_use;
} RenderJob;

typedef struct {
    RenderJob jobs[RQ_MAX_JOBS];
    uint64_t max_budget_mb;
    uint64_t reserved_mb;
    size_t count;
} RenderQueue;

int rq_init(RenderQueue *q, uint64_t max_budget_mb);
int rq_enqueue(RenderQueue *q, uint64_t job_id, uint32_t width, uint32_t height, uint64_t est_mb);
int rq_pop_next(RenderQueue *q, RenderJob *out_job);
int rq_mark_done(RenderQueue *q, uint64_t job_id);
int rq_mark_failed(RenderQueue *q, uint64_t job_id);
size_t rq_count_by_state(const RenderQueue *q, RqJobState state);
int rq_summary_line(const RenderQueue *q, char *buf, size_t buf_len);

#endif

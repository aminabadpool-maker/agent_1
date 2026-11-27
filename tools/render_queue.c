#include "render_queue.h"
#include <stdio.h>
#include <string.h>

static int rq_find_index(const RenderQueue *q, uint64_t job_id, size_t *out_idx) {
    if (!q || !out_idx || job_id == 0U) {
        return 0;
    }
    for (size_t i = 0U; i < RQ_MAX_JOBS; ++i) {
        if (q->jobs[i].in_use && q->jobs[i].job_id == job_id) {
            *out_idx = i;
            return 1;
        }
    }
    return 0;
}

int rq_init(RenderQueue *q, uint64_t max_budget_mb) {
    if (!q || max_budget_mb == 0U) {
        return RQ_ERR_INVALID;
    }
    memset(q, 0, sizeof(*q));
    q->max_budget_mb = max_budget_mb;
    q->reserved_mb = 0U;
    q->count = 0U;
    return RQ_OK;
}

int rq_enqueue(RenderQueue *q, uint64_t job_id, uint32_t width, uint32_t height, uint64_t est_mb) {
    size_t idx = 0U;
    size_t free_slot = RQ_MAX_JOBS;

    if (!q || q->max_budget_mb == 0U || job_id == 0U || width == 0U || height == 0U || est_mb == 0U) {
        return RQ_ERR_INVALID;
    }
    if (rq_find_index(q, job_id, &idx)) {
        return RQ_ERR_DUPLICATE;
    }
    if (q->count >= RQ_MAX_JOBS) {
        return RQ_ERR_FULL;
    }
    if (est_mb > UINT64_MAX - q->reserved_mb) {
        return RQ_ERR_BUDGET;
    }
    if (q->reserved_mb + est_mb > q->max_budget_mb) {
        return RQ_ERR_BUDGET;
    }

    for (size_t i = 0U; i < RQ_MAX_JOBS; ++i) {
        if (!q->jobs[i].in_use) {
            free_slot = i;
            break;
        }
    }
    if (free_slot == RQ_MAX_JOBS) {
        return RQ_ERR_FULL;
    }

    q->jobs[free_slot].job_id = job_id;
    q->jobs[free_slot].width = width;
    q->jobs[free_slot].height = height;
    q->jobs[free_slot].est_mb = est_mb;
    q->jobs[free_slot].state = RQ_JOB_QUEUED;
    q->jobs[free_slot].in_use = 1;

    q->reserved_mb += est_mb;
    q->count += 1U;
    return RQ_OK;
}

int rq_pop_next(RenderQueue *q, RenderJob *out_job) {
    if (!q || !out_job || q->max_budget_mb == 0U) {
        return RQ_ERR_INVALID;
    }

    for (size_t i = 0U; i < RQ_MAX_JOBS; ++i) {
        if (q->jobs[i].in_use && q->jobs[i].state == RQ_JOB_QUEUED) {
            q->jobs[i].state = RQ_JOB_ACTIVE;
            *out_job = q->jobs[i];
            return RQ_OK;
        }
    }
    return RQ_ERR_NOT_FOUND;
}

static int rq_set_terminal_state(RenderQueue *q, uint64_t job_id, RqJobState next_state) {
    size_t idx = 0U;

    if (!q || q->max_budget_mb == 0U || job_id == 0U) {
        return RQ_ERR_INVALID;
    }
    if (!rq_find_index(q, job_id, &idx)) {
        return RQ_ERR_NOT_FOUND;
    }
    if (q->jobs[idx].state != RQ_JOB_ACTIVE) {
        return RQ_ERR_STATE;
    }

    q->jobs[idx].state = next_state;
    if (q->reserved_mb >= q->jobs[idx].est_mb) {
        q->reserved_mb -= q->jobs[idx].est_mb;
    } else {
        q->reserved_mb = 0U;
    }
    return RQ_OK;
}

int rq_mark_done(RenderQueue *q, uint64_t job_id) {
    return rq_set_terminal_state(q, job_id, RQ_JOB_DONE);
}

int rq_mark_failed(RenderQueue *q, uint64_t job_id) {
    return rq_set_terminal_state(q, job_id, RQ_JOB_FAILED);
}

size_t rq_count_by_state(const RenderQueue *q, RqJobState state) {
    size_t n = 0U;

    if (!q) {
        return 0U;
    }
    for (size_t i = 0U; i < RQ_MAX_JOBS; ++i) {
        if (q->jobs[i].in_use && q->jobs[i].state == state) {
            n += 1U;
        }
    }
    return n;
}

int rq_summary_line(const RenderQueue *q, char *buf, size_t buf_len) {
    int written = 0;

    if (!q || !buf || buf_len == 0U) {
        return RQ_ERR_INVALID;
    }

    written = snprintf(
        buf,
        buf_len,
        "queued=%zu active=%zu done=%zu failed=%zu reserved_mb=%llu/%llu",
        rq_count_by_state(q, RQ_JOB_QUEUED),
        rq_count_by_state(q, RQ_JOB_ACTIVE),
        rq_count_by_state(q, RQ_JOB_DONE),
        rq_count_by_state(q, RQ_JOB_FAILED),
        (unsigned long long)q->reserved_mb,
        (unsigned long long)q->max_budget_mb
    );

    if (written < 0 || (size_t)written >= buf_len) {
        return RQ_ERR_INVALID;
    }
    return RQ_OK;
}

#include "render_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    RenderQueue q;
    RenderJob job;
    char line[160];

    assert(rq_init(NULL, 1024U) == RQ_ERR_INVALID);
    assert(rq_init(&q, 0U) == RQ_ERR_INVALID);
    assert(rq_init(&q, 4096U) == RQ_OK);

    assert(rq_pop_next(&q, &job) == RQ_ERR_NOT_FOUND);
    assert(rq_enqueue(&q, 0U, 1920U, 1080U, 256U) == RQ_ERR_INVALID);
    assert(rq_enqueue(&q, 1U, 0U, 1080U, 256U) == RQ_ERR_INVALID);
    assert(rq_enqueue(&q, 1U, 1920U, 0U, 256U) == RQ_ERR_INVALID);
    assert(rq_enqueue(&q, 1U, 1920U, 1080U, 0U) == RQ_ERR_INVALID);

    assert(rq_enqueue(&q, 101U, 3840U, 2160U, 2048U) == RQ_OK);
    assert(rq_enqueue(&q, 101U, 3840U, 2160U, 512U) == RQ_ERR_DUPLICATE);
    assert(rq_enqueue(&q, 102U, 3840U, 2160U, 2049U) == RQ_ERR_BUDGET);
    assert(rq_enqueue(&q, 102U, 3840U, 2160U, 1536U) == RQ_OK);
    assert(q.reserved_mb == 3584U);

    assert(rq_mark_done(&q, 101U) == RQ_ERR_STATE);
    assert(rq_pop_next(&q, &job) == RQ_OK);
    assert(job.job_id == 101U);
    assert(job.state == RQ_JOB_ACTIVE);
    assert(rq_mark_done(&q, 101U) == RQ_OK);
    assert(q.reserved_mb == 1536U);
    assert(rq_mark_done(&q, 101U) == RQ_ERR_STATE);

    assert(rq_pop_next(&q, &job) == RQ_OK);
    assert(job.job_id == 102U);
    assert(rq_mark_failed(&q, 102U) == RQ_OK);
    assert(q.reserved_mb == 0U);

    assert(rq_summary_line(&q, line, sizeof(line)) == RQ_OK);
    assert(strcmp(line, "queued=0 active=0 done=1 failed=1 reserved_mb=0/4096") == 0);

    printf("test_render_queue: PASS (%s)\n", line);
    return 0;
}

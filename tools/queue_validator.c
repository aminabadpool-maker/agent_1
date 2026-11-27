#include "render_common.h"
#include <inttypes.h>

#define MAX_JOBS 512
#define MAX_LINE 512
#define MAX_ID 64
#define MAX_TARGET 256
#define MAX_PRIO 10000U
#define MAX_SINGLE_JOB_SEC 86400ULL

typedef struct {
    char job_id[MAX_ID];
    uint32_t priority;
    uint64_t est_sec;
} seen_job_t;

static bool has_duplicate_id(const seen_job_t *arr, size_t n, const char *id) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(arr[i].job_id, id) == 0) return true;
    }
    return false;
}

static int validate_queue(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "status=INVALID reason=open_failed path=%s\n", path);
        return 2;
    }

    seen_job_t seen[MAX_JOBS];
    size_t job_count = 0;
    size_t line_no = 0;
    size_t invalid_lines = 0;
    uint64_t total_sec = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), fp) != NULL) {
        line_no++;
        if (!strchr(line, '\n') && !feof(fp)) {
            fprintf(stderr, "line=%zu status=INVALID reason=line_too_long\n", line_no);
            fclose(fp);
            return 1;
        }
        rc_trim_eol(line);
        if (rc_is_blank_or_comment(line)) continue;

        if (job_count >= MAX_JOBS) {
            fprintf(stderr, "line=%zu status=INVALID reason=max_jobs_exceeded max=%d\n", line_no, MAX_JOBS);
            fclose(fp);
            return 1;
        }

        char *p1 = strchr(line, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        char *p3 = p2 ? strchr(p2 + 1, '|') : NULL;
        if (!p1 || !p2 || !p3 || strchr(p3 + 1, '|') != NULL) {
            fprintf(stderr, "line=%zu status=INVALID reason=field_count_mismatch\n", line_no);
            invalid_lines++;
            continue;
        }

        *p1 = '\0';
        *p2 = '\0';
        *p3 = '\0';

        const char *jid = line;
        const char *tgt = p1 + 1;
        const char *prio_s = p2 + 1;
        const char *sec_s = p3 + 1;

        if (!*jid || strlen(jid) >= MAX_ID) {
            fprintf(stderr, "line=%zu status=INVALID reason=bad_job_id\n", line_no);
            invalid_lines++;
            continue;
        }
        if (!*tgt || strlen(tgt) >= MAX_TARGET) {
            fprintf(stderr, "line=%zu status=INVALID reason=bad_target\n", line_no);
            invalid_lines++;
            continue;
        }
        if (has_duplicate_id(seen, job_count, jid)) {
            fprintf(stderr, "line=%zu job=%s status=INVALID reason=duplicate_job_id\n", line_no, jid);
            invalid_lines++;
            continue;
        }

        uint32_t prio = 0;
        uint64_t est = 0;
        if (rc_parse_u32(prio_s, &prio) != 0 || prio > MAX_PRIO) {
            fprintf(stderr, "line=%zu job=%s status=INVALID reason=bad_priority\n", line_no, jid);
            invalid_lines++;
            continue;
        }
        if (rc_parse_u64(sec_s, &est) != 0 || est == 0 || est > MAX_SINGLE_JOB_SEC) {
            fprintf(stderr, "line=%zu job=%s status=INVALID reason=bad_est_sec\n", line_no, jid);
            invalid_lines++;
            continue;
        }
        if (UINT64_MAX - total_sec < est) {
            fprintf(stderr, "line=%zu job=%s status=INVALID reason=total_sec_overflow\n", line_no, jid);
            invalid_lines++;
            continue;
        }

        snprintf(seen[job_count].job_id, sizeof(seen[job_count].job_id), "%s", jid);
        seen[job_count].priority = prio;
        seen[job_count].est_sec = est;
        total_sec += est;
        job_count++;
    }

    int err = ferror(fp);
    fclose(fp);
    if (err != 0) {
        fprintf(stderr, "status=INVALID reason=io_error\n");
        return 2;
    }

    if (invalid_lines > 0 || job_count == 0) {
        printf("status=INVALID valid_jobs=%zu invalid_lines=%zu total_est_sec=%" PRIu64 "\n",
               job_count, invalid_lines, total_sec);
        return 1;
    }

    printf("status=VALID valid_jobs=%zu invalid_lines=0 total_est_sec=%" PRIu64 "\n",
           job_count, total_sec);
    return 0;
}

static int run_self_tests(void) {
    const char *tmp = getenv("TMPDIR");
    const char *base = (tmp && *tmp) ? tmp : ".";

    char good_path[512];
    char bad_path[512];
    snprintf(good_path, sizeof(good_path), "%s/agent1_qval_good.txt", base);
    snprintf(bad_path, sizeof(bad_path), "%s/agent1_qval_bad.txt", base);

    FILE *fg = fopen(good_path, "w");
    if (!fg) return 1;
    fputs("# valid queue\njob_1|out_01.png|1|10\njob_2|out_02.png|2|25\n", fg);
    fclose(fg);

    int rc_good = validate_queue(good_path);
    remove(good_path);
    if (rc_good != 0) {
        fprintf(stderr, "[FAIL] expected VALID queue\n");
        return 1;
    }

    FILE *fb = fopen(bad_path, "w");
    if (!fb) return 1;
    fputs("dup_job|out_01.png|1|10\ndup_job|out_02.png|2|15\n", fb);
    fclose(fb);

    int rc_bad = validate_queue(bad_path);
    remove(bad_path);
    if (rc_bad == 0) {
        fprintf(stderr, "[FAIL] expected INVALID queue for duplicate job_id\n");
        return 1;
    }

    printf("[SELF-TEST PASS] queue_validator checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }
    if (argc == 2) {
        return validate_queue(argv[1]);
    }
    fprintf(stderr, "Usage:\n  %s --test\n  %s <queue_file>\n", argv[0], argv[0]);
    return 2;
}

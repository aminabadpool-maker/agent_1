#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOBS 256
#define MAX_LINE 512
#define MAX_ID 64
#define MAX_TARGET 256

typedef struct {
    size_t seq;
    uint32_t priority;
    uint64_t est_sec;
    char job_id[MAX_ID];
    char target[MAX_TARGET];
} render_job_t;

static void trim_eol(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static bool is_blank_or_comment(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return (*s == '\0' || *s == '#');
}

static int parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > UINT32_MAX) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static int cmp_jobs(const void *a, const void *b) {
    const render_job_t *ja = (const render_job_t *)a;
    const render_job_t *jb = (const render_job_t *)b;
    if (ja->priority < jb->priority) return -1;
    if (ja->priority > jb->priority) return 1;
    if (ja->seq < jb->seq) return -1;
    if (ja->seq > jb->seq) return 1;
    return 0;
}

static int process_queue_file(const char *path, bool quiet) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "status=ERR reason=open_failed path=%s\n", path);
        return 2;
    }

    render_job_t jobs[MAX_JOBS];
    size_t count = 0;
    uint64_t total_sec = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (!strchr(line, '\n') && !feof(fp)) {
            fprintf(stderr, "status=ERR reason=line_too_long\n");
            fclose(fp);
            return 1;
        }
        trim_eol(line);
        if (is_blank_or_comment(line)) continue;

        if (count >= MAX_JOBS) {
            fprintf(stderr, "status=ERR reason=queue_full max_jobs=%d\n", MAX_JOBS);
            fclose(fp);
            return 1;
        }

        /* Format: job_id|target|priority|est_sec */
        char *p1 = strchr(line, '|');
        if (!p1) {
            fprintf(stderr, "status=ERR reason=bad_format\n");
            fclose(fp);
            return 1;
        }
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '|');
        if (!p2) {
            fprintf(stderr, "status=ERR reason=bad_format\n");
            fclose(fp);
            return 1;
        }
        *p2 = '\0';
        char *p3 = strchr(p2 + 1, '|');
        if (!p3) {
            fprintf(stderr, "status=ERR reason=bad_format\n");
            fclose(fp);
            return 1;
        }
        *p3 = '\0';

        const char *jid = line;
        const char *tgt = p1 + 1;
        const char *prio_s = p2 + 1;
        const char *sec_s = p3 + 1;

        if (!*jid || !*tgt || strlen(jid) >= MAX_ID || strlen(tgt) >= MAX_TARGET) {
            fprintf(stderr, "status=ERR reason=invalid_field_length\n");
            fclose(fp);
            return 1;
        }

        uint32_t prio = 0;
        uint64_t est = 0;
        if (parse_u32(prio_s, &prio) != 0 || parse_u64(sec_s, &est) != 0) {
            fprintf(stderr, "status=ERR reason=bad_number\n");
            fclose(fp);
            return 1;
        }

        if (UINT64_MAX - total_sec < est) {
            fprintf(stderr, "status=ERR reason=est_sec_overflow\n");
            fclose(fp);
            return 1;
        }

        render_job_t *j = &jobs[count];
        j->seq = count;
        j->priority = prio;
        j->est_sec = est;
        snprintf(j->job_id, sizeof(j->job_id), "%s", jid);
        snprintf(j->target, sizeof(j->target), "%s", tgt);

        total_sec += est;
        count++;
    }

    int err = ferror(fp);
    fclose(fp);
    if (err != 0) {
        fprintf(stderr, "status=ERR reason=read_error\n");
        return 2;
    }

    if (count > 1) {
        qsort(jobs, count, sizeof(render_job_t), cmp_jobs);
    }

    if (!quiet) {
        for (size_t i = 0; i < count; i++) {
            printf("order=%zu job=%s prio=%" PRIu32 " est_sec=%" PRIu64 " target=%s\n",
                   i + 1, jobs[i].job_id, jobs[i].priority, jobs[i].est_sec, jobs[i].target);
        }
        printf("queue_summary jobs=%zu total_est_sec=%" PRIu64 "\n", count, total_sec);
    }
    return 0;
}

static int run_self_tests(void) {
    const char *tmp = getenv("TMPDIR");
    const char *base = (tmp && *tmp) ? tmp : ".";
    char qpath[512];
    snprintf(qpath, sizeof(qpath), "%s/agent1_batch_queue_test.txt", base);

    FILE *fp = fopen(qpath, "w");
    if (!fp) return 1;
    fputs("# sample queue\n"
          "job_b|frame_002.png|10|15\n"
          "job_a|frame_001.png|1|20\n"
          "job_c|frame_003.png|10|12\n", fp);
    fclose(fp);

    int rc = process_queue_file(qpath, false);
    remove(qpath);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] render_batch_queue self-test failed\n");
        return 1;
    }

    printf("[SELF-TEST PASS] render_batch_queue checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }
    if (argc == 2) {
        return process_queue_file(argv[1], false);
    }
    if (argc == 3 && strcmp(argv[1], "--quiet") == 0) {
        return process_queue_file(argv[2], true);
    }

    fprintf(stderr,
            "Usage:\n"
            "  %s --test\n"
            "  %s <queue_file>\n"
            "  %s --quiet <queue_file>\n",
            argv[0], argv[0], argv[0]);
    return 2;
}

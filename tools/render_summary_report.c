#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long val = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)val;
    return 0;
}

static bool has_non_zero_header(const char *path, size_t *out_read) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    unsigned char buf[256];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    int err = ferror(fp);
    fclose(fp);

    if (err != 0 || n == 0) return false;
    if (out_read) *out_read = n;

    for (size_t i = 0; i < n; i++) {
        if (buf[i] != 0x00) return true;
    }
    return false;
}

static int emit_render_summary(const char *job_id,
                               const char *artifact_path,
                               uint64_t elapsed_sec,
                               uint64_t min_bytes,
                               uint64_t max_bytes) {
    if (!job_id || !*job_id || !artifact_path || !*artifact_path) {
        fprintf(stderr, "status=INVALID_ARGS reason=missing_job_or_artifact\n");
        return 2;
    }

    struct stat st;
    if (stat(artifact_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        printf("job=%s status=FAIL elapsed_sec=%" PRIu64 " reason=missing_or_not_regular\n",
               job_id, elapsed_sec);
        return 1;
    }

    if (st.st_size <= 0) {
        printf("job=%s status=FAIL elapsed_sec=%" PRIu64 " bytes=0 reason=empty_file\n",
               job_id, elapsed_sec);
        return 1;
    }

    uint64_t sz = (uint64_t)st.st_size;
    if (sz < min_bytes) {
        printf("job=%s status=FAIL elapsed_sec=%" PRIu64 " bytes=%" PRIu64 " min_bytes=%" PRIu64 " reason=below_min_size\n",
               job_id, elapsed_sec, sz, min_bytes);
        return 1;
    }

    if (max_bytes > 0 && sz > max_bytes) {
        printf("job=%s status=FAIL elapsed_sec=%" PRIu64 " bytes=%" PRIu64 " max_bytes=%" PRIu64 " reason=above_max_size\n",
               job_id, elapsed_sec, sz, max_bytes);
        return 1;
    }

    size_t probed = 0;
    if (!has_non_zero_header(artifact_path, &probed)) {
        printf("job=%s status=FAIL elapsed_sec=%" PRIu64 " bytes=%" PRIu64 " reason=blank_or_zero_header\n",
               job_id, elapsed_sec, sz);
        return 1;
    }

    printf("job=%s status=OK elapsed_sec=%" PRIu64 " bytes=%" PRIu64 " header_bytes=%" PRIu64 " artifact=%s\n",
           job_id, elapsed_sec, sz, (uint64_t)probed, artifact_path);
    return 0;
}

static int run_self_tests(void) {
    const char *tmp = getenv("TMPDIR");
    const char *base = (tmp && *tmp) ? tmp : ".";
    char sample[512];
    snprintf(sample, sizeof(sample), "%s/agent1_summary_test.ppm", base);

    FILE *fp = fopen(sample, "wb");
    if (!fp) return 1;
    fputs("P3\n1 1\n255\n120 200 255\n", fp);
    fclose(fp);

    int rc = emit_render_summary("smoke_01", sample, 2, 8, 65536);
    remove(sample);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] expected OK summary for valid artifact\n");
        return 1;
    }

    printf("[SELF-TEST PASS] render_summary_report checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    /* Usage: render_summary_report <job_id> <artifact_path> <elapsed_sec> <min_bytes> <max_bytes> */
    if (argc != 6) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <job_id> <artifact_path> <elapsed_sec> <min_bytes> <max_bytes>\n",
                argv[0], argv[0]);
        return 2;
    }

    uint64_t elapsed = 0, min_b = 0, max_b = 0;
    if (parse_u64(argv[3], &elapsed) != 0 ||
        parse_u64(argv[4], &min_b) != 0 ||
        parse_u64(argv[5], &max_b) != 0) {
        fprintf(stderr, "status=INVALID_ARGS reason=bad_numeric_param\n");
        return 2;
    }

    return emit_render_summary(argv[1], argv[2], elapsed, min_b, max_b);
}

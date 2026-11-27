#include "render_common.h"
#include <inttypes.h>

static int emit_render_summary(const char *job_id,
                               const char *artifact_path,
                               uint64_t elapsed_sec,
                               uint64_t min_bytes,
                               uint64_t max_bytes) {
    if (!job_id || !*job_id || !artifact_path || !*artifact_path) {
        fprintf(stderr, "status=INVALID_ARGS reason=missing_job_or_artifact\n");
        return 2;
    }

    uint64_t sz = 0;
    size_t probed = 0;
    if (!rc_inspect_artifact(artifact_path, &sz, &probed)) {
        printf("job=%s status=FAIL elapsed_sec=%" PRIu64 " reason=invalid_or_empty_artifact\n",
               job_id, elapsed_sec);
        return 1;
    }

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

    if (argc != 6) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <job_id> <artifact_path> <elapsed_sec> <min_bytes> <max_bytes>\n",
                argv[0], argv[0]);
        return 2;
    }

    uint64_t elapsed = 0, min_b = 0, max_b = 0;
    if (rc_parse_u64(argv[3], &elapsed) != 0 ||
        rc_parse_u64(argv[4], &min_b) != 0 ||
        rc_parse_u64(argv[5], &max_b) != 0) {
        fprintf(stderr, "status=INVALID_ARGS reason=bad_numeric_param\n");
        return 2;
    }

    return emit_render_summary(argv[1], argv[2], elapsed, min_b, max_b);
}

#include "render_common.h"
#include <inttypes.h>

static int summarize_paths(int count, char **paths) {
    if (count <= 0) {
        fprintf(stderr, "status=INVALID_ARGS reason=no_artifacts\n");
        return 2;
    }

    size_t ok_count = 0;
    size_t bad_count = 0;
    uint64_t total_bytes = 0;

    for (int i = 0; i < count; i++) {
        uint64_t sz = 0;
        if (rc_inspect_artifact(paths[i], &sz, NULL)) {
            ok_count++;
            if (UINT64_MAX - total_bytes >= sz) {
                total_bytes += sz;
            }
            printf("item=%d status=OK bytes=%" PRIu64 " path=%s\n", i + 1, sz, paths[i]);
        } else {
            bad_count++;
            printf("item=%d status=FAIL path=%s\n", i + 1, paths[i]);
        }
    }

    printf("batch_report total=%d ok=%zu fail=%zu total_bytes=%" PRIu64 "\n",
           count, ok_count, bad_count, total_bytes);
    return (bad_count == 0) ? 0 : 1;
}

static int run_self_tests(void) {
    const char *tmp = getenv("TMPDIR");
    const char *base = (tmp && *tmp) ? tmp : ".";

    char f1[512], f2[512];
    snprintf(f1, sizeof(f1), "%s/agent1_batch_r1.ppm", base);
    snprintf(f2, sizeof(f2), "%s/agent1_batch_r2.ppm", base);

    FILE *a = fopen(f1, "wb");
    FILE *b = fopen(f2, "wb");
    if (!a || !b) {
        if (a) fclose(a);
        if (b) fclose(b);
        return 1;
    }
    fputs("P3\n1 1\n255\n10 20 30\n", a);
    fputs("P3\n1 1\n255\n40 50 60\n", b);
    fclose(a);
    fclose(b);

    char *items[2] = { f1, f2 };
    int rc = summarize_paths(2, items);
    remove(f1);
    remove(f2);

    if (rc != 0) {
        fprintf(stderr, "[FAIL] expected batch report success\n");
        return 1;
    }

    printf("[SELF-TEST PASS] render_batch_report checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }
    if (argc < 2) {
        fprintf(stderr, "Usage:\n  %s --test\n  %s <artifact1> [artifact2 ...]\n", argv[0], argv[0]);
        return 2;
    }
    return summarize_paths(argc - 1, argv + 1);
}

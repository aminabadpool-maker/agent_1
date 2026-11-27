#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool file_looks_healthy(const char *path, uint64_t *out_bytes) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    unsigned char buf[128];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    int err = ferror(fp);
    fclose(fp);

    if (err != 0 || n == 0) return false;

    bool non_zero = false;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != 0x00) {
            non_zero = true;
            break;
        }
    }
    if (!non_zero) return false;

    if (out_bytes) *out_bytes = (uint64_t)st.st_size;
    return true;
}

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
        if (file_looks_healthy(paths[i], &sz)) {
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

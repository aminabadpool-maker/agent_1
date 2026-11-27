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

static int verify_render_artifact(const char *path,
                                  uint64_t min_bytes,
                                  uint64_t max_bytes,
                                  char *reason,
                                  size_t rlen) {
    if (!path || !*path || !reason || rlen == 0) return 2;

    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(reason, rlen, "artifact not found or stat failed");
        return 1;
    }

    if (!S_ISREG(st.st_mode)) {
        snprintf(reason, rlen, "artifact is not a regular file");
        return 1;
    }

    if (st.st_size <= 0) {
        snprintf(reason, rlen, "artifact file is empty (0 bytes)");
        return 1;
    }

    uint64_t size_u64 = (uint64_t)st.st_size;
    if (size_u64 < min_bytes) {
        snprintf(reason, rlen, "artifact too small (size=%" PRIu64 "B, min=%" PRIu64 "B)",
                 size_u64, min_bytes);
        return 1;
    }

    if (max_bytes > 0 && size_u64 > max_bytes) {
        snprintf(reason, rlen, "artifact exceeds max_bytes (size=%" PRIu64 "B, max=%" PRIu64 "B)",
                 size_u64, max_bytes);
        return 1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(reason, rlen, "failed to open artifact for verification");
        return 1;
    }

    unsigned char probe[256];
    size_t nread = fread(probe, 1, sizeof(probe), fp);
    int io_err = ferror(fp);
    fclose(fp);

    if (io_err != 0 || nread == 0) {
        snprintf(reason, rlen, "failed reading artifact header bytes");
        return 1;
    }

    /* Reject header blocks made exclusively of NUL bytes */
    bool all_zero = true;
    for (size_t i = 0; i < nread; i++) {
        if (probe[i] != 0x00) {
            all_zero = false;
            break;
        }
    }

    if (all_zero) {
        snprintf(reason, rlen, "artifact header is all zero bytes (likely truncated/blank)");
        return 1;
    }

    snprintf(reason, rlen, "ok(size=%" PRIu64 "B, header_checked=%" PRIu64 "B)",
             size_u64, (uint64_t)nread);
    return 0;
}

static int run_self_tests(void) {
    const char *tmp_env = getenv("TMPDIR");
    const char *base = (tmp_env && tmp_env[0] != '\0') ? tmp_env : ".";

    char good_path[512];
    char zero_path[512];
    snprintf(good_path, sizeof(good_path), "%s/agent1_good_render.ppm", base);
    snprintf(zero_path, sizeof(zero_path), "%s/agent1_zero_render.ppm", base);

    FILE *fg = fopen(good_path, "wb");
    if (!fg) return 1;
    fputs("P3\n2 1\n255\n255 0 0 0 255 0\n", fg);
    fclose(fg);

    FILE *fz = fopen(zero_path, "wb");
    if (!fz) {
        remove(good_path);
        return 1;
    }
    unsigned char zeros[64] = {0};
    fwrite(zeros, 1, sizeof(zeros), fz);
    fclose(fz);

    char reason[256];
    int rc_good = verify_render_artifact(good_path, 8, 1048576, reason, sizeof(reason));
    int rc_zero = verify_render_artifact(zero_path, 8, 1048576, reason, sizeof(reason));

    remove(good_path);
    remove(zero_path);

    if (rc_good != 0) {
        fprintf(stderr, "[FAIL] Valid render artifact rejected: %s\n", reason);
        return 1;
    }
    if (rc_zero == 0) {
        fprintf(stderr, "[FAIL] All-zero render artifact unexpectedly passed\n");
        return 1;
    }

    printf("[SELF-TEST PASS] render_artifact_guard checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    /* Usage: render_artifact_guard <artifact_path> <min_bytes> <max_bytes> */
    if (argc != 4) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <artifact_path> <min_bytes> <max_bytes>\n",
                argv[0], argv[0]);
        return 2;
    }

    uint64_t min_b = 0, max_b = 0;
    if (parse_u64(argv[2], &min_b) != 0 || parse_u64(argv[3], &max_b) != 0) {
        fprintf(stderr, "[ARTIFACT BLOCK] Invalid min_bytes or max_bytes.\n");
        return 2;
    }

    if (max_b > 0 && min_b > max_b) {
        fprintf(stderr, "[ARTIFACT BLOCK] min_bytes cannot exceed max_bytes.\n");
        return 2;
    }

    char reason[256];
    int rc = verify_render_artifact(argv[1], min_b, max_b, reason, sizeof(reason));
    printf("[%s] %s\n", rc == 0 ? "ARTIFACT PASS" : "ARTIFACT BLOCK", reason);
    return rc;
}

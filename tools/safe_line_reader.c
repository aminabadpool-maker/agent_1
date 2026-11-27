#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Resolve a safe temporary directory across Termux and standard Linux:
 * Prefers TMPDIR if set and non-empty, otherwise falls back to safe_fallback_dir.
 */
static const char *resolve_temp_dir(const char *safe_fallback_dir) {
    const char *env_tmp = getenv("TMPDIR");
    if (env_tmp && env_tmp[0] != '\0') {
        return env_tmp;
    }
    if (safe_fallback_dir && safe_fallback_dir[0] != '\0') {
        return safe_fallback_dir;
    }
    return ".";
}

/*
 * Safe bounded file & line reader in C11:
 * - Rejects non-regular files or files exceeding max_file_bytes
 * - Enforces a strict per-line byte limit to prevent unbounded line buffers
 */
static int read_bounded_file(const char *path,
                             size_t max_file_bytes,
                             size_t max_line_bytes,
                             const char **err_reason) {
    if (!path || !*path || max_line_bytes < 2) {
        if (err_reason) *err_reason = "invalid path or max_line_bytes";
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (err_reason) *err_reason = "stat failed on target file";
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        if (err_reason) *err_reason = "target is not a regular file";
        return -1;
    }

    if (st.st_size < 0) {
        if (err_reason) *err_reason = "negative file size reported";
        return -1;
    }

    uint64_t fsize_u64 = (uint64_t)st.st_size;
    if (max_file_bytes > 0 && fsize_u64 > (uint64_t)max_file_bytes) {
        if (err_reason) *err_reason = "file exceeds max_file_bytes limit";
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (err_reason) *err_reason = "failed to open file";
        return -1;
    }

    char *line_buf = (char *)malloc(max_line_bytes);
    if (!line_buf) {
        fclose(fp);
        if (err_reason) *err_reason = "memory allocation failed for line buffer";
        return -1;
    }

    uint64_t line_no = 0;
    uint64_t total_bytes = 0;
    int status = 0;

    while (fgets(line_buf, (int)max_line_bytes, fp) != NULL) {
        line_no++;
        size_t len = strlen(line_buf);
        total_bytes += (uint64_t)len;

        if (max_file_bytes > 0 && total_bytes > (uint64_t)max_file_bytes) {
            if (err_reason) *err_reason = "stream exceeded max_file_bytes limit";
            status = -1;
            break;
        }

        /* If newline not seen and EOF not reached, line exceeded max_line_bytes - 1 */
        if (len == max_line_bytes - 1 && line_buf[len - 1] != '\n' && !feof(fp)) {
            if (err_reason) *err_reason = "line exceeded max_line_bytes limit";
            status = -1;
            break;
        }
    }

    if (status == 0 && ferror(fp)) {
        if (err_reason) *err_reason = "I/O read error while scanning file";
        status = -1;
    }

    free(line_buf);
    fclose(fp);

    if (status == 0 && err_reason) {
        *err_reason = "ok";
    }

    if (status == 0) {
        printf("[READ PASS] lines=%" PRIu64 " bytes=%" PRIu64 "\n", line_no, total_bytes);
    }
    return status;
}

static int run_self_tests(void) {
    const char *tmp_dir = resolve_temp_dir(".");
    char tmp_path[512];
    int written = snprintf(tmp_path, sizeof(tmp_path), "%s/agent1_safe_line_test.txt", tmp_dir);
    if (written <= 0 || (size_t)written >= sizeof(tmp_path)) {
        fprintf(stderr, "[FAIL] Temp path too long\n");
        return 1;
    }

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "[FAIL] Could not create temp test file at %s\n", tmp_path);
        return 1;
    }
    fputs("line1\nline2_short\n", f);
    fclose(f);

    const char *reason = NULL;
    if (read_bounded_file(tmp_path, 4096, 64, &reason) != 0) {
        remove(tmp_path);
        fprintf(stderr, "[FAIL] Valid file blocked: %s\n", reason ? reason : "unknown");
        return 1;
    }

    /* Expect block when max_line_bytes is tiny (5 bytes) */
    if (read_bounded_file(tmp_path, 4096, 5, &reason) == 0) {
        remove(tmp_path);
        fprintf(stderr, "[FAIL] Long line should have been blocked\n");
        return 1;
    }

    remove(tmp_path);
    printf("[SELF-TEST PASS] safe_line_reader checks passed using %s\n", tmp_path);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        return run_self_tests();
    }

    if (argc != 4) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <file_path> <max_file_bytes> <max_line_bytes>\n",
                argv[0], argv[0]);
        return 2;
    }

    char *end1 = NULL;
    char *end2 = NULL;
    errno = 0;
    unsigned long long max_f = strtoull(argv[2], &end1, 10);
    unsigned long long max_l = strtoull(argv[3], &end2, 10);
    if (errno != 0 || end1 == argv[2] || *end1 != '\0' ||
        end2 == argv[3] || *end2 != '\0' || max_l > (unsigned long long)SIZE_MAX) {
        fprintf(stderr, "[BLOCK] Invalid numeric limits.\n");
        return 2;
    }

    const char *reason = NULL;
    size_t cap_f = (max_f > (unsigned long long)SIZE_MAX) ? SIZE_MAX : (size_t)max_f;
    if (read_bounded_file(argv[1], cap_f, (size_t)max_l, &reason) != 0) {
        fprintf(stderr, "[READ BLOCK] reason=%s\n", reason ? reason : "denied");
        return 1;
    }

    return 0;
}

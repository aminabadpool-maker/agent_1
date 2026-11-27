#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
 * Check if candidate_path is safely inside allowed_root:
 * - Rejects empty/NULL paths, root "/", "$HOME", ".", or ".."
 * - Resolves both paths with realpath() when possible so symlinks/.. cannot escape
 * - Ensures candidate_path is strictly a child of allowed_root (not equal to root)
 */
static bool is_safe_subpath(const char *candidate_path,
                           const char *allowed_root,
                           const char **reason) {
    if (!candidate_path || candidate_path[0] == '\0' ||
        !allowed_root || allowed_root[0] == '\0') {
        if (reason) *reason = "empty candidate or root path";
        return false;
    }

    if (strcmp(candidate_path, "/") == 0 ||
        strcmp(candidate_path, ".") == 0 ||
        strcmp(candidate_path, "..") == 0) {
        if (reason) *reason = "forbidden generic/root path";
        return false;
    }

    const char *home_env = getenv("HOME");
    if (home_env && home_env[0] != '\0' && strcmp(candidate_path, home_env) == 0) {
        if (reason) *reason = "refusing target equal to HOME";
        return false;
    }

    char resolved_root[PATH_MAX];
    char resolved_cand[PATH_MAX];

    if (!realpath(allowed_root, resolved_root)) {
        if (reason) *reason = "allowed_root does not exist or cannot be resolved";
        return false;
    }

    if (!realpath(candidate_path, resolved_cand)) {
        if (reason) *reason = "candidate path does not exist or cannot be resolved";
        return false;
    }

    if (strcmp(resolved_root, "/") == 0) {
        if (reason) *reason = "allowed_root cannot be filesystem root /";
        return false;
    }

    if (home_env && home_env[0] != '\0' && strcmp(resolved_cand, home_env) == 0) {
        if (reason) *reason = "resolved candidate equals HOME";
        return false;
    }

    size_t root_len = strlen(resolved_root);
    size_t cand_len = strlen(resolved_cand);

    /* Must be strictly deeper than allowed_root, not equal to it */
    if (cand_len <= root_len) {
        if (reason) *reason = "candidate is not strictly inside allowed_root";
        return false;
    }

    if (strncmp(resolved_cand, resolved_root, root_len) != 0) {
        if (reason) *reason = "candidate escapes allowed_root";
        return false;
    }

    if (resolved_cand[root_len] != '/') {
        if (reason) *reason = "path boundary mismatch under allowed_root";
        return false;
    }

    if (reason) *reason = "ok";
    return true;
}

static int run_self_tests(void) {
    const char *tmp_env = getenv("TMPDIR");
    const char *base = (tmp_env && tmp_env[0] != '\0') ? tmp_env : ".";

    char root_dir[PATH_MAX];
    char valid_sub[PATH_MAX];
    int n1 = snprintf(root_dir, sizeof(root_dir), "%s/agent1_path_guard_root", base);
    int n2 = snprintf(valid_sub, sizeof(valid_sub), "%s/agent1_path_guard_root/job_1", base);
    if (n1 <= 0 || (size_t)n1 >= sizeof(root_dir) ||
        n2 <= 0 || (size_t)n2 >= sizeof(valid_sub)) {
        fprintf(stderr, "[FAIL] Test path buffer overflow\n");
        return 1;
    }

    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", valid_sub);
    if (system(cmd) != 0) {
        fprintf(stderr, "[FAIL] Could not create test directories\n");
        return 1;
    }

    const char *reason = NULL;

    /* Test 1: Valid subdirectory inside root_dir -> must pass */
    if (!is_safe_subpath(valid_sub, root_dir, &reason)) {
        fprintf(stderr, "[FAIL] Valid subpath blocked: %s\n", reason ? reason : "unknown");
        return 1;
    }

    /* Test 2: root_dir itself -> must block */
    if (is_safe_subpath(root_dir, root_dir, &reason)) {
        fprintf(stderr, "[FAIL] Root directory itself should not be allowed\n");
        return 1;
    }

    /* Test 3: "/" -> must block */
    if (is_safe_subpath("/", root_dir, &reason)) {
        fprintf(stderr, "[FAIL] '/' should be blocked\n");
        return 1;
    }

    /* Cleanup test dirs safely */
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", root_dir);
    (void)system(cmd);

    printf("[SELF-TEST PASS] safe_path_guard checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        return run_self_tests();
    }

    if (argc != 3) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <candidate_path> <allowed_root_dir>\n",
                argv[0], argv[0]);
        return 2;
    }

    const char *reason = NULL;
    if (!is_safe_subpath(argv[1], argv[2], &reason)) {
        fprintf(stderr, "[PATH BLOCK] reason=%s\n", reason ? reason : "denied");
        return 1;
    }

    printf("[PATH PASS] candidate=%s root=%s\n", argv[1], argv[2]);
    return 0;
}

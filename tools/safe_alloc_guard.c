#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Safe allocation guard for C11:
 * - Prevents count * elem_size integer overflow
 * - Enforces an explicit ceiling (max_bytes) to protect low-RAM devices
 * - Zeroes allocated memory via calloc
 */
void *safe_calloc_guarded(size_t count, size_t elem_size, size_t max_bytes, const char **reason) {
    if (count == 0 || elem_size == 0) {
        if (reason) *reason = "zero count or zero element size";
        return NULL;
    }

    if (count > SIZE_MAX / elem_size) {
        if (reason) *reason = "size_t multiplication overflow";
        return NULL;
    }

    size_t total = count * elem_size;
    if (max_bytes > 0 && total > max_bytes) {
        if (reason) *reason = "requested bytes exceed safety cap";
        return NULL;
    }

    void *ptr = calloc(1, total);
    if (!ptr) {
        if (reason) *reason = "system allocator returned NULL";
        return NULL;
    }

    if (reason) *reason = "ok";
    return ptr;
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

static int run_self_tests(void) {
    const char *reason = NULL;

    /* Test 1: Valid allocation within 1 MiB cap */
    void *ok_ptr = safe_calloc_guarded(1024, 64, 1024u * 1024u, &reason);
    if (!ok_ptr) {
        fprintf(stderr, "[FAIL] Test 1 expected pass, got: %s\n", reason ? reason : "unknown");
        return 1;
    }
    free(ok_ptr);

    /* Test 2: Exceeds cap -> must block */
    void *over_ptr = safe_calloc_guarded(1024, 2048, 1024u * 1024u, &reason);
    if (over_ptr) {
        free(over_ptr);
        fprintf(stderr, "[FAIL] Test 2 expected cap block\n");
        return 1;
    }

    /* Test 3: Overflow -> must block */
    void *ovf_ptr = safe_calloc_guarded(SIZE_MAX / 2u + 2u, 4u, 0, &reason);
    if (ovf_ptr) {
        free(ovf_ptr);
        fprintf(stderr, "[FAIL] Test 3 expected overflow block\n");
        return 1;
    }

    printf("[SELF-TEST PASS] safe_calloc_guarded checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    if (argc != 4) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <count> <elem_bytes> <max_bytes>\n",
                argv[0], argv[0]);
        return 2;
    }

    uint64_t c_u64 = 0, e_u64 = 0, m_u64 = 0;
    if (parse_u64(argv[1], &c_u64) != 0 ||
        parse_u64(argv[2], &e_u64) != 0 ||
        parse_u64(argv[3], &m_u64) != 0) {
        fprintf(stderr, "[BLOCK] Invalid unsigned integer arguments.\n");
        return 2;
    }

    if (c_u64 > (uint64_t)SIZE_MAX ||
        e_u64 > (uint64_t)SIZE_MAX ||
        m_u64 > (uint64_t)SIZE_MAX) {
        fprintf(stderr, "[BLOCK] Argument exceeds platform SIZE_MAX.\n");
        return 1;
    }

    const char *reason = NULL;
    void *buf = safe_calloc_guarded((size_t)c_u64, (size_t)e_u64, (size_t)m_u64, &reason);
    if (!buf) {
        printf("[GUARD BLOCK] reason=%s count=%" PRIu64 " elem=%" PRIu64 " cap=%" PRIu64 "\n",
               reason ? reason : "denied", c_u64, e_u64, m_u64);
        return 1;
    }

    printf("[GUARD PASS] allocated=%" PRIu64 " bytes (cap=%" PRIu64 ")\n",
           c_u64 * e_u64, m_u64);
    free(buf);
    return 0;
}

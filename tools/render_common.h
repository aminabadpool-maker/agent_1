#ifndef AGENT1_RENDER_COMMON_H
#define AGENT1_RENDER_COMMON_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline void rc_trim_eol(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static inline bool rc_is_blank_or_comment(const char *s) {
    if (!s) return true;
    while (*s == ' ' || *s == '\t') s++;
    return (*s == '\0' || *s == '#');
}

static inline int rc_parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s || !out) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > UINT32_MAX) return -1;
    *out = (uint32_t)v;
    return 0;
}

static inline int rc_parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s || !out) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static inline bool rc_inspect_artifact(const char *path,
                                       uint64_t *out_bytes,
                                       size_t *out_header_bytes) {
    if (!path || !*path) return false;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    unsigned char buf[256];
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
    if (out_header_bytes) *out_header_bytes = n;
    return true;
}

#endif /* AGENT1_RENDER_COMMON_H */

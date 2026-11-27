#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_TAG_NAME 64
#define MAX_TAG_DEPTH 64

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long val = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)val;
    return 0;
}

static bool is_valid_tag_char(unsigned char ch) {
    return isalnum(ch) || ch == '_' || ch == '-' || ch == ':';
}

static int validate_xml_scene(const char *path, uint64_t max_bytes, char *reason, size_t rlen) {
    if (!path || !*path || !reason || rlen == 0) return 2;

    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(reason, rlen, "cannot stat scene file");
        return 1;
    }
    if (!S_ISREG(st.st_mode)) {
        snprintf(reason, rlen, "scene path is not a regular file");
        return 1;
    }
    if (st.st_size <= 0) {
        snprintf(reason, rlen, "scene file is empty");
        return 1;
    }
    uint64_t fsize = (uint64_t)st.st_size;
    if (max_bytes > 0 && fsize > max_bytes) {
        snprintf(reason, rlen, "scene size (%" PRIu64 "B) exceeds max (%" PRIu64 "B)", fsize, max_bytes);
        return 1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(reason, rlen, "failed to open scene file");
        return 1;
    }

    char stack[MAX_TAG_DEPTH][MAX_TAG_NAME];
    int depth = 0;
    int ch = 0;
    uint64_t total_read = 0;
    int rc = 0;

    while ((ch = fgetc(fp)) != EOF) {
        total_read++;
        if (max_bytes > 0 && total_read > max_bytes) {
            snprintf(reason, rlen, "stream exceeded max_bytes");
            rc = 1;
            break;
        }
        if (ch == '\0') {
            snprintf(reason, rlen, "unexpected NUL byte in XML");
            rc = 1;
            break;
        }
        if (ch < 0x20 && ch != '\n' && ch != '\r' && ch != '\t') {
            snprintf(reason, rlen, "forbidden control character 0x%02x", (unsigned int)ch);
            rc = 1;
            break;
        }

        if (ch == '<') {
            int next = fgetc(fp);
            if (next == EOF) {
                snprintf(reason, rlen, "unterminated '<' at EOF");
                rc = 1;
                break;
            }
            total_read++;

            /* Skip XML declaration/comments/PIs starting with '?' or '!' */
            if (next == '?' || next == '!') {
                int prev = 0;
                bool closed = false;
                while ((ch = fgetc(fp)) != EOF) {
                    total_read++;
                    if (prev == '-' && ch == '-') {
                        int after = fgetc(fp);
                        if (after == '>') {
                            total_read++;
                            closed = true;
                            break;
                        }
                        if (after != EOF) ungetc(after, fp);
                    }
                    if (ch == '>') {
                        closed = true;
                        break;
                    }
                    prev = ch;
                }
                if (!closed) {
                    snprintf(reason, rlen, "unclosed XML declaration or comment");
                    rc = 1;
                    break;
                }
                continue;
            }

            bool is_closing = (next == '/');
            char tag[MAX_TAG_NAME];
            size_t tlen = 0;

            if (!is_closing) {
                if (!is_valid_tag_char((unsigned char)next)) {
                    snprintf(reason, rlen, "invalid start char in tag");
                    rc = 1;
                    break;
                }
                tag[tlen++] = (char)next;
            }

            bool self_closing = false;
            bool tag_ended = false;

            while ((ch = fgetc(fp)) != EOF) {
                total_read++;
                if (ch == '>') {
                    tag_ended = true;
                    break;
                }
                if (ch == '/') {
                    self_closing = true;
                    continue;
                }
                if (isspace((unsigned char)ch) && tlen > 0) {
                    /* Read until '>' while tracking '/' */
                    while ((ch = fgetc(fp)) != EOF) {
                        total_read++;
                        if (ch == '/') self_closing = true;
                        else if (ch == '>') {
                            tag_ended = true;
                            break;
                        }
                    }
                    break;
                }
                if (tlen + 1 >= MAX_TAG_NAME || !is_valid_tag_char((unsigned char)ch)) {
                    snprintf(reason, rlen, "invalid or overly long tag name");
                    rc = 1;
                    break;
                }
                tag[tlen++] = (char)ch;
            }

            if (rc != 0) break;
            if (!tag_ended || tlen == 0) {
                snprintf(reason, rlen, "malformed XML tag");
                rc = 1;
                break;
            }
            tag[tlen] = '\0';

            if (is_closing) {
                if (self_closing || depth <= 0 || strcmp(stack[depth - 1], tag) != 0) {
                    snprintf(reason, rlen, "mismatched closing tag </%s>", tag);
                    rc = 1;
                    break;
                }
                depth--;
            } else if (!self_closing) {
                if (depth >= MAX_TAG_DEPTH) {
                    snprintf(reason, rlen, "XML nesting depth exceeded %d", MAX_TAG_DEPTH);
                    rc = 1;
                    break;
                }
                snprintf(stack[depth], MAX_TAG_NAME, "%s", tag);
                depth++;
            }
        }
    }

    if (rc == 0 && ferror(fp)) {
        snprintf(reason, rlen, "I/O error reading XML file");
        rc = 1;
    }
    if (rc == 0 && depth != 0) {
        snprintf(reason, rlen, "unclosed XML tags remain (depth=%d)", depth);
        rc = 1;
    }

    fclose(fp);
    if (rc == 0) {
        snprintf(reason, rlen, "ok(size=%" PRIu64 "B)", total_read);
    }
    return rc;
}

static int run_self_tests(void) {
    const char *tmp_env = getenv("TMPDIR");
    const char *base = (tmp_env && tmp_env[0] != '\0') ? tmp_env : ".";
    char good_path[512];
    char bad_path[512];
    snprintf(good_path, sizeof(good_path), "%s/agent1_good_scene.xml", base);
    snprintf(bad_path, sizeof(bad_path), "%s/agent1_bad_scene.xml", base);

    FILE *fg = fopen(good_path, "wb");
    if (!fg) return 1;
    fputs("<scene><mesh name=\"cube\"/><camera fov=\"60\"/></scene>\n", fg);
    fclose(fg);

    FILE *fb = fopen(bad_path, "wb");
    if (!fb) {
        remove(good_path);
        return 1;
    }
    fputs("<scene><mesh></scene>\n", fb);
    fclose(fb);

    char reason[256];
    int rc_good = validate_xml_scene(good_path, 65536, reason, sizeof(reason));
    int rc_bad = validate_xml_scene(bad_path, 65536, reason, sizeof(reason));

    remove(good_path);
    remove(bad_path);

    if (rc_good != 0) {
        fprintf(stderr, "[FAIL] Good XML rejected: %s\n", reason);
        return 1;
    }
    if (rc_bad == 0) {
        fprintf(stderr, "[FAIL] Broken XML unexpectedly accepted\n");
        return 1;
    }

    printf("[SELF-TEST PASS] xml_scene_guard checks passed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        return run_self_tests();
    }

    if (argc != 3) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --test\n"
                "  %s <scene_xml_path> <max_bytes>\n",
                argv[0], argv[0]);
        return 2;
    }

    uint64_t max_b = 0;
    if (parse_u64(argv[2], &max_b) != 0) {
        fprintf(stderr, "[XML BLOCK] Invalid max_bytes argument.\n");
        return 2;
    }

    char reason[256];
    int rc = validate_xml_scene(argv[1], max_b, reason, sizeof(reason));
    printf("[%s] %s\n", rc == 0 ? "XML PASS" : "XML BLOCK", reason);
    return rc;
}

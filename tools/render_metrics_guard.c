#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *path;
    uint64_t size_bytes;
    int valid;
} frame_metric_t;

static int inspect_single_frame(const char *path, uint64_t *out_size) {
    struct stat st;
    FILE *fp;
    char magic[3];
    size_t nread;

    if (!path || !*path || !out_size) {
        return -1;
    }

    if (stat(path, &st) != 0) {
        return -1;
    }

    if (!S_ISREG(st.st_mode) || st.st_size <= 0) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    nread = fread(magic, 1, 2, fp);
    fclose(fp);

    if (nread < 2 || magic[0] != 'P' || (magic[1] != '3' && magic[1] != '6')) {
        return -1;
    }

    *out_size = (uint64_t)st.st_size;
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [--max-drift-pct N] <frame1.ppm> [frame2.ppm ...]\n", prog);
}

int main(int argc, char **argv) {
    int max_drift_pct = 15;
    int arg_idx = 1;
    int frame_count;
    frame_metric_t *frames;
    uint64_t total_bytes = 0;
    uint64_t min_bytes = UINT64_MAX;
    uint64_t max_bytes = 0;
    uint64_t avg_bytes = 0;
    int invalid_count = 0;
    int drift_alerts = 0;
    int i;

    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "--max-drift-pct") == 0) {
        char *endptr = NULL;
        long parsed;
        if (argc < 4) {
            print_usage(argv[0]);
            return 2;
        }
        errno = 0;
        parsed = strtol(argv[2], &endptr, 10);
        if (errno != 0 || endptr == argv[2] || *endptr != '\0' || parsed < 0 || parsed > 1000) {
            fprintf(stderr, "[ERROR] Invalid --max-drift-pct value: %s\n", argv[2]);
            return 2;
        }
        max_drift_pct = (int)parsed;
        arg_idx = 3;
    }

    frame_count = argc - arg_idx;
    if (frame_count <= 0) {
        print_usage(argv[0]);
        return 2;
    }

    frames = calloc((size_t)frame_count, sizeof(frame_metric_t));
    if (!frames) {
        fprintf(stderr, "[ERROR] Out of memory allocating frame metrics\n");
        return 1;
    }

    for (i = 0; i < frame_count; ++i) {
        uint64_t fsize = 0;
        frames[i].path = argv[arg_idx + i];
        if (inspect_single_frame(frames[i].path, &fsize) == 0) {
            frames[i].size_bytes = fsize;
            frames[i].valid = 1;
            total_bytes += fsize;
            if (fsize < min_bytes) {
                min_bytes = fsize;
            }
            if (fsize > max_bytes) {
                max_bytes = fsize;
            }
        } else {
            frames[i].size_bytes = 0;
            frames[i].valid = 0;
            invalid_count++;
        }
    }

    if (frame_count - invalid_count > 0) {
        avg_bytes = total_bytes / (uint64_t)(frame_count - invalid_count);
    } else {
        min_bytes = 0;
        max_bytes = 0;
        avg_bytes = 0;
    }

    if (avg_bytes > 0) {
        for (i = 0; i < frame_count; ++i) {
            uint64_t diff = 0;
            uint64_t allowed_diff = (avg_bytes * (uint64_t)max_drift_pct) / 100ULL;
            if (!frames[i].valid) {
                continue;
            }
            if (frames[i].size_bytes > avg_bytes) {
                diff = frames[i].size_bytes - avg_bytes;
            } else {
                diff = avg_bytes - frames[i].size_bytes;
            }
            if (diff > allowed_diff) {
                drift_alerts++;
                fprintf(stderr,
                        "[DRIFT-ALERT] frame=%s size=%" PRIu64 " avg=%" PRIu64 " drift_limit_pct=%d\n",
                        frames[i].path,
                        frames[i].size_bytes,
                        avg_bytes,
                        max_drift_pct);
            }
        }
    }

    printf("render_metrics frames=%d valid=%d invalid=%d total_bytes=%" PRIu64
           " avg_bytes=%" PRIu64 " min_bytes=%" PRIu64 " max_bytes=%" PRIu64
           " drift_alerts=%d max_drift_pct=%d\n",
           frame_count,
           frame_count - invalid_count,
           invalid_count,
           total_bytes,
           avg_bytes,
           min_bytes,
           max_bytes,
           drift_alerts,
           max_drift_pct);

    free(frames);

    if (invalid_count > 0 || drift_alerts > 0) {
        return 1;
    }
    return 0;
}

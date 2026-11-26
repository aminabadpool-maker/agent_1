#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int a, b;
} Edge;

static unsigned char fb[H][W][3];

static void clear_fb(unsigned char r, unsigned char g, unsigned char b) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            fb[y][x][0] = r;
            fb[y][x][1] = g;
            fb[y][x][2] = b;
        }
    }
}

static void put_px(int x, int y, unsigned char r, unsigned char g, unsigned char b) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    fb[y][x][0] = r;
    fb[y][x][1] = g;
    fb[y][x][2] = b;
}

static void draw_line(int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static Vec3 rot_xy(Vec3 p, float ax, float ay) {
    float cx = cosf(ax), sx = sinf(ax);
    float y1 = p.y * cx - p.z * sx;
    float z1 = p.y * sx + p.z * cx;
    float cy = cosf(ay), sy = sinf(ay);
    float x2 = p.x * cy + z1 * sy;
    float z2 = -p.x * sy + z1 * cy;
    Vec3 out = { x2, y1, z2 };
    return out;
}

static void save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen ppm");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(fb, 1, sizeof(fb), f);
    fclose(f);
}

int main(int argc, char **argv) {
    float angle = (argc > 1) ? strtof(argv[1], NULL) : 0.7f;
    const char *out_path = (argc > 2) ? argv[2] : "render.ppm";

    Vec3 v[8] = {
        {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
        {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
    };
    Edge e[12] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    clear_fb(18, 22, 30);

    float fov = 220.0f;
    float cam_z = 4.2f;

    for (int i = 0; i < 12; i++) {
        Vec3 p0 = rot_xy(v[e[i].a], angle * 0.6f, angle);
        Vec3 p1 = rot_xy(v[e[i].b], angle * 0.6f, angle);
        float z0 = p0.z + cam_z;
        float z1 = p1.z + cam_z;
        if (z0 <= 0.2f || z1 <= 0.2f) continue;

        int x0 = (int)(W * 0.5f + (p0.x * fov) / z0);
        int y0 = (int)(H * 0.5f - (p0.y * fov) / z0);
        int x1 = (int)(W * 0.5f + (p1.x * fov) / z1);
        int y1 = (int)(H * 0.5f - (p1.y * fov) / z1);

        draw_line(x0, y0, x1, y1, 80, 240, 140);
    }

    save_ppm(out_path);
    printf("Rendered %s (angle=%.2f)\n", out_path, angle);
    return 0;
}

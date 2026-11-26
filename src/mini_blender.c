#define _POSIX_C_SOURCE 200809L
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 640
#define H 480
#define MAX_V 32768
#define MAX_T 65536

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int v0, v1, v2;
    unsigned char r, g, b;
} Tri;

typedef struct {
    Vec3 verts[MAX_V];
    int vcount;
    Tri tris[MAX_T];
    int tcount;
    int obj_count;
} Scene;

static unsigned char fb[H][W][3];
static float zbuf[H][W];

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static Vec3 v3_sub(Vec3 a, Vec3 b) {
    Vec3 r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

static Vec3 v3_cross(Vec3 a, Vec3 b) {
    Vec3 r = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
    return r;
}

static float v3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 v3_norm(Vec3 a) {
    float len = sqrtf(v3_dot(a, a));
    if (len < 1e-6f) {
        Vec3 z = { 0.0f, 0.0f, 1.0f };
        return z;
    }
    Vec3 r = { a.x / len, a.y / len, a.z / len };
    return r;
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

static void scene_add_box(Scene *s, Vec3 center, Vec3 size, float yaw, unsigned char r, unsigned char g, unsigned char b) {
    if (s->vcount + 8 > MAX_V || s->tcount + 12 > MAX_T) return;
    int base = s->vcount;
    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;

    Vec3 local[8] = {
        { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
        { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
    };

    for (int i = 0; i < 8; i++) {
        Vec3 rot = rot_xy(local[i], 0.0f, yaw);
        s->verts[s->vcount].x = center.x + rot.x;
        s->verts[s->vcount].y = center.y + rot.y;
        s->verts[s->vcount].z = center.z + rot.z;
        s->vcount++;
    }

    int idx[12][3] = {
        {0, 2, 1}, {0, 3, 2},
        {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4},
        {2, 3, 7}, {2, 7, 6},
        {0, 4, 7}, {0, 7, 3},
        {1, 2, 6}, {1, 6, 5}
    };

    for (int i = 0; i < 12; i++) {
        Tri t;
        t.v0 = base + idx[i][0];
        t.v1 = base + idx[i][1];
        t.v2 = base + idx[i][2];
        t.r = r;
        t.g = g;
        t.b = b;
        s->tris[s->tcount++] = t;
    }
    s->obj_count++;
}

static void build_default_stress_scene(Scene *s) {
    s->vcount = 0;
    s->tcount = 0;
    s->obj_count = 0;

    Vec3 ground_c = { 0.0f, -1.3f, 0.0f };
    Vec3 ground_s = { 18.0f, 0.25f, 18.0f };
    scene_add_box(s, ground_c, ground_s, 0.0f, 70, 82, 96);

    int grid = 14;
    for (int gx = 0; gx < grid; gx++) {
        for (int gz = 0; gz < grid; gz++) {
            float fx = -6.5f + (float)gx * 1.0f;
            float fz = -6.5f + (float)gz * 1.0f;
            float h = 0.45f + 0.40f * fabsf(sinf((float)gx * 0.7f + (float)gz * 0.5f));
            Vec3 c = { fx, -1.175f + h * 0.5f, fz };
            Vec3 sz = { 0.56f, h, 0.56f };
            unsigned char r = (unsigned char)(70 + ((gx * 17 + gz * 29) % 150));
            unsigned char g = (unsigned char)(85 + ((gx * 31 + gz * 13) % 140));
            unsigned char b = (unsigned char)(100 + ((gx * 11 + gz * 23) % 130));
            scene_add_box(s, c, sz, 0.15f * (float)((gx + gz) % 6), r, g, b);
        }
    }
}

static float extract_attr_float(const char *line, const char *key, float def_val) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s=\"", key);
    const char *p = strstr(line, pat);
    if (!p) return def_val;
    p += strlen(pat);
    return strtof(p, NULL);
}

static int extract_attr_int(const char *line, const char *key, int def_val) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s=\"", key);
    const char *p = strstr(line, pat);
    if (!p) return def_val;
    p += strlen(pat);
    return (int)strtol(p, NULL, 10);
}

static int load_scene_xml_like(Scene *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    s->vcount = 0;
    s->tcount = 0;
    s->obj_count = 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "<object") != NULL) {
            float x = extract_attr_float(line, "x", 0.0f);
            float y = extract_attr_float(line, "y", 0.0f);
            float z = extract_attr_float(line, "z", 0.0f);
            float sx = extract_attr_float(line, "sx", 1.0f);
            float sy = extract_attr_float(line, "sy", 1.0f);
            float sz = extract_attr_float(line, "sz", 1.0f);
            float yaw = extract_attr_float(line, "yaw", 0.0f);
            int r = extract_attr_int(line, "r", 160);
            int g = extract_attr_int(line, "g", 160);
            int b = extract_attr_int(line, "b", 160);

            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;

            Vec3 c = { x, y, z };
            Vec3 sz_v = { sx, sy, sz };
            scene_add_box(s, c, sz_v, yaw, (unsigned char)r, (unsigned char)g, (unsigned char)b);
        }
    }

    fclose(f);
    return (s->obj_count > 0) ? 1 : 0;
}

static void clear_buffers(unsigned char r, unsigned char g, unsigned char b) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            fb[y][x][0] = r;
            fb[y][x][1] = g;
            fb[y][x][2] = b;
            zbuf[y][x] = FLT_MAX;
        }
    }
}

static float edge_fn(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void raster_tri(Vec3 p0, Vec3 p1, Vec3 p2, float fov, float cam_z, unsigned char r, unsigned char g, unsigned char b, Vec3 key_light, Vec3 fill_light) {
    float z0 = p0.z + cam_z;
    float z1 = p1.z + cam_z;
    float z2 = p2.z + cam_z;
    if (z0 <= 0.2f || z1 <= 0.2f || z2 <= 0.2f) return;

    Vec3 n = v3_norm(v3_cross(v3_sub(p1, p0), v3_sub(p2, p0)));
    float key_d = v3_dot(n, key_light);
    if (key_d < 0.0f) key_d = 0.0f;

    float fill_d = v3_dot(n, fill_light);
    if (fill_d < 0.0f) fill_d = 0.0f;

    float shade = 0.18f + 0.65f * key_d + 0.22f * fill_d;
    if (shade > 1.0f) shade = 1.0f;

    unsigned char sr = (unsigned char)(r * shade);
    unsigned char sg = (unsigned char)(g * shade);
    unsigned char sb = (unsigned char)(b * shade);

    float sx0 = W * 0.5f + (p0.x * fov) / z0;
    float sy0 = H * 0.5f - (p0.y * fov) / z0;
    float sx1 = W * 0.5f + (p1.x * fov) / z1;
    float sy1 = H * 0.5f - (p1.y * fov) / z1;
    float sx2 = W * 0.5f + (p2.x * fov) / z2;
    float sy2 = H * 0.5f - (p2.y * fov) / z2;

    float area = edge_fn(sx0, sy0, sx1, sy1, sx2, sy2);
    if (fabsf(area) < 1e-4f) return;

    int min_x = (int)floorf(fminf(sx0, fminf(sx1, sx2)));
    int max_x = (int)ceilf(fmaxf(sx0, fmaxf(sx1, sx2)));
    int min_y = (int)floorf(fminf(sy0, fminf(sy1, sy2)));
    int max_y = (int)ceilf(fmaxf(sy0, fmaxf(sy1, sy2)));

    if (min_x < 0) min_x = 0;
    if (max_x >= W) max_x = W - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= H) max_y = H - 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;

            float w0 = edge_fn(sx1, sy1, sx2, sy2, px, py);
            float w1 = edge_fn(sx2, sy2, sx0, sy0, px, py);
            float w2 = edge_fn(sx0, sy0, sx1, sy1, px, py);

            int inside = (area > 0.0f) ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                       : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) continue;

            w0 /= area;
            w1 /= area;
            w2 /= area;

            float z = w0 * z0 + w1 * z1 + w2 * z2;
            if (z < zbuf[y][x]) {
                zbuf[y][x] = z;
                fb[y][x][0] = sr;
                fb[y][x][1] = sg;
                fb[y][x][2] = sb;
            }
        }
    }
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

static void render_frame(const Scene *s, float cam_yaw, float cam_pitch, float cam_z, const char *out_path) {
    clear_buffers(18, 24, 34);
    Vec3 raw_key = { 0.45f, 0.78f, 0.50f };
    Vec3 raw_fill = { -0.60f, 0.35f, -0.45f };
    Vec3 key_light = v3_norm(raw_key);
    Vec3 fill_light = v3_norm(raw_fill);
    float fov = 420.0f;

    for (int i = 0; i < s->tcount; i++) {
        Tri t = s->tris[i];
        Vec3 p0 = rot_xy(s->verts[t.v0], cam_pitch, cam_yaw);
        Vec3 p1 = rot_xy(s->verts[t.v1], cam_pitch, cam_yaw);
        Vec3 p2 = rot_xy(s->verts[t.v2], cam_pitch, cam_yaw);
        raster_tri(p0, p1, p2, fov, cam_z, t.r, t.g, t.b, key_light, fill_light);
    }

    save_ppm(out_path);
}

int main(int argc, char **argv) {
    int start_frame = (argc > 1) ? atoi(argv[1]) : 0;
    int count = (argc > 2) ? atoi(argv[2]) : 16;
    int total_cycle = (argc > 3) ? atoi(argv[3]) : 32;
    const char *out_dir = (argc > 4) ? argv[4] : "renders";
    const char *scene_path = (argc > 5) ? argv[5] : NULL;

    if (start_frame < 0) start_frame = 0;
    if (count < 1) count = 1;
    if (total_cycle < 1) total_cycle = 32;

    Scene s;
    if (scene_path && load_scene_xml_like(&s, scene_path)) {
        printf("Loaded XML scene '%s': %d objs, %d verts, %d tris\n", scene_path, s.obj_count, s.vcount, s.tcount);
    } else {
        build_default_stress_scene(&s);
        printf("Built default stress scene: %d objs, %d verts, %d tris\n", s.obj_count, s.vcount, s.tcount);
    }

    double batch_start = now_sec();
    for (int i = 0; i < count; i++) {
        int f = start_frame + i;
        double f_start = now_sec();
        float t = ((float)(f % total_cycle) / (float)total_cycle) * 6.2831853f;
        char path[256];
        snprintf(path, sizeof(path), "%s/frame_%04d.ppm", out_dir, f);
        render_frame(&s, t, 0.34f, 12.2f, path);
        double dt = now_sec() - f_start;
        printf("Frame %04d -> %s (%.3fs)\n", f, path, dt);
        fflush(stdout);
    }

    double total_dt = now_sec() - batch_start;
    printf("Batch finished: %d frames in %.3fs (avg %.3fs/frame)\n", count, total_dt, total_dt / (double)count);
    return 0;
}

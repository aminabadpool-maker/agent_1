#define _POSIX_C_SOURCE 200809L
#include <errno.h>
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
#define MAX_LIGHTS 8

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int v0, v1, v2;
    unsigned char r, g, b;
} Tri;

typedef struct {
    Vec3 pos;
    Vec3 color;
    float intensity;
} Light;

typedef struct {
    Vec3 pos;
    float yaw;
    float pitch;
    float fov;
} Camera;

typedef struct {
    Vec3 verts[MAX_V];
    int vcount;
    Tri tris[MAX_T];
    int tcount;
    int obj_count;
    Camera cam;
    Light lights[MAX_LIGHTS];
    int light_count;
} Scene;

static unsigned char fb[H][W][3];
static float zbuf[H][W];

static double now_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static unsigned char clamp_u8(float v) {
    if (!isfinite(v) || v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (unsigned char)(v + 0.5f);
}

static float clamp_pos_float(float v, float min_v, float def_v) {
    if (!isfinite(v) || v < min_v) return def_v;
    return v;
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
    float len2 = v3_dot(a, a);
    if (!isfinite(len2) || len2 < 1e-12f) {
        Vec3 z = { 0.0f, 1.0f, 0.0f };
        return z;
    }
    float inv_len = 1.0f / sqrtf(len2);
    Vec3 r = { a.x * inv_len, a.y * inv_len, a.z * inv_len };
    return r;
}

static Vec3 rot_xyz(Vec3 p, float rx, float ry, float rz) {
    float cx = cosf(rx), sx = sinf(rx);
    float y1 = p.y * cx - p.z * sx;
    float z1 = p.y * sx + p.z * cx;
    float x1 = p.x;

    float cy = cosf(ry), sy = sinf(ry);
    float x2 = x1 * cy + z1 * sy;
    float z2 = -x1 * sy + z1 * cy;
    float y2 = y1;

    float cz = cosf(rz), sz = sinf(rz);
    float x3 = x2 * cz - y2 * sz;
    float y3 = x2 * sz + y2 * cz;

    Vec3 out = { x3, y3, z2 };
    return out;
}

static void scene_init_defaults(Scene *s) {
    memset(s, 0, sizeof(*s));
    s->cam.pos.x = 0.0f;
    s->cam.pos.y = 1.4f;
    s->cam.pos.z = 12.2f;
    s->cam.yaw = 0.0f;
    s->cam.pitch = -0.14f;
    s->cam.fov = 420.0f;

    s->lights[0].pos.x = 6.0f;
    s->lights[0].pos.y = 9.0f;
    s->lights[0].pos.z = 7.0f;
    s->lights[0].color.x = 1.0f;
    s->lights[0].color.y = 0.96f;
    s->lights[0].color.z = 0.88f;
    s->lights[0].intensity = 0.85f;

    s->lights[1].pos.x = -7.0f;
    s->lights[1].pos.y = 4.0f;
    s->lights[1].pos.z = -5.0f;
    s->lights[1].color.x = 0.55f;
    s->lights[1].color.y = 0.72f;
    s->lights[1].color.z = 0.95f;
    s->lights[1].intensity = 0.35f;

    s->light_count = 2;
}

static int scene_add_box(Scene *s, Vec3 center, Vec3 size, Vec3 rot_angles, unsigned char r, unsigned char g, unsigned char b) {
    if (!s) return 0;
    if (s->vcount + 8 > MAX_V || s->tcount + 12 > MAX_T) {
        fprintf(stderr, "Warning: scene capacity reached (v=%d/%d, t=%d/%d), skipping extra object\n",
                s->vcount, MAX_V, s->tcount, MAX_T);
        return 0;
    }

    int base = s->vcount;
    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;

    Vec3 local[8] = {
        { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
        { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
    };

    for (int i = 0; i < 8; i++) {
        Vec3 rot = rot_xyz(local[i], rot_angles.x, rot_angles.y, rot_angles.z);
        s->verts[s->vcount].x = center.x + rot.x;
        s->verts[s->vcount].y = center.y + rot.y;
        s->verts[s->vcount].z = center.z + rot.z;
        s->vcount++;
    }

    static const int idx[12][3] = {
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
    return 1;
}

static void build_default_stress_scene(Scene *s) {
    scene_init_defaults(s);

    Vec3 ground_c = { 0.0f, -1.3f, 0.0f };
    Vec3 ground_s = { 18.0f, 0.25f, 18.0f };
    Vec3 ground_r = { 0.0f, 0.0f, 0.0f };
    scene_add_box(s, ground_c, ground_s, ground_r, 70, 82, 96);

    const int grid = 14;
    for (int gx = 0; gx < grid; gx++) {
        for (int gz = 0; gz < grid; gz++) {
            float fx = -6.5f + (float)gx * 1.0f;
            float fz = -6.5f + (float)gz * 1.0f;
            float h = 0.45f + 0.40f * fabsf(sinf((float)gx * 0.7f + (float)gz * 0.5f));
            Vec3 c = { fx, -1.175f + h * 0.5f, fz };
            Vec3 sz = { 0.56f, h, 0.56f };
            Vec3 rot = { 0.0f, 0.15f * (float)((gx + gz) % 6), 0.0f };
            unsigned char r = (unsigned char)(70 + ((gx * 17 + gz * 29) % 150));
            unsigned char g = (unsigned char)(85 + ((gx * 31 + gz * 13) % 140));
            unsigned char b = (unsigned char)(100 + ((gx * 11 + gz * 23) % 130));
            scene_add_box(s, c, sz, rot, r, g, b);
        }
    }
}

static float extract_attr_float(const char *line, const char *key, float def_val) {
    char pat[64];
    int n = snprintf(pat, sizeof(pat), "%s=\"", key);
    if (n <= 0 || (size_t)n >= sizeof(pat)) return def_val;
    const char *p = strstr(line, pat);
    if (!p) return def_val;
    p += (size_t)n;
    errno = 0;
    char *endptr = NULL;
    float val = strtof(p, &endptr);
    if (p == endptr || errno != 0 || !isfinite(val)) return def_val;
    return val;
}

static int extract_attr_int(const char *line, const char *key, int def_val) {
    char pat[64];
    int n = snprintf(pat, sizeof(pat), "%s=\"", key);
    if (n <= 0 || (size_t)n >= sizeof(pat)) return def_val;
    const char *p = strstr(line, pat);
    if (!p) return def_val;
    p += (size_t)n;
    errno = 0;
    char *endptr = NULL;
    long val = strtol(p, &endptr, 10);
    if (p == endptr || errno != 0) return def_val;
    return (int)val;
}

static int load_scene_xml(Scene *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Warning: cannot open scene file '%s': %s\n", path, strerror(errno));
        return 0;
    }

    scene_init_defaults(s);
    int parsed_custom_light = 0;

    char line[1024];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strstr(line, "<camera") != NULL) {
            s->cam.pos.x = extract_attr_float(line, "x", s->cam.pos.x);
            s->cam.pos.y = extract_attr_float(line, "y", s->cam.pos.y);
            s->cam.pos.z = extract_attr_float(line, "z", s->cam.pos.z);
            s->cam.yaw = extract_attr_float(line, "yaw", s->cam.yaw);
            s->cam.pitch = extract_attr_float(line, "pitch", s->cam.pitch);
            s->cam.fov = clamp_pos_float(extract_attr_float(line, "fov", s->cam.fov), 50.0f, 420.0f);
        } else if (strstr(line, "<light") != NULL) {
            if (!parsed_custom_light) {
                s->light_count = 0;
                parsed_custom_light = 1;
            }
            if (s->light_count < MAX_LIGHTS) {
                Light lt;
                lt.pos.x = extract_attr_float(line, "x", 5.0f);
                lt.pos.y = extract_attr_float(line, "y", 8.0f);
                lt.pos.z = extract_attr_float(line, "z", 6.0f);
                int lr = extract_attr_int(line, "r", 255);
                int lg = extract_attr_int(line, "g", 255);
                int lb = extract_attr_int(line, "b", 255);
                lt.color.x = (float)clamp_u8((float)lr) / 255.0f;
                lt.color.y = (float)clamp_u8((float)lg) / 255.0f;
                lt.color.z = (float)clamp_u8((float)lb) / 255.0f;
                lt.intensity = clamp_pos_float(extract_attr_float(line, "intensity", 0.8f), 0.0f, 0.8f);
                s->lights[s->light_count++] = lt;
            }
        } else if (strstr(line, "<object") != NULL) {
            Vec3 c = {
                extract_attr_float(line, "x", 0.0f),
                extract_attr_float(line, "y", 0.0f),
                extract_attr_float(line, "z", 0.0f)
            };
            Vec3 sz = {
                clamp_pos_float(extract_attr_float(line, "sx", 1.0f), 0.01f, 1.0f),
                clamp_pos_float(extract_attr_float(line, "sy", 1.0f), 0.01f, 1.0f),
                clamp_pos_float(extract_attr_float(line, "sz", 1.0f), 0.01f, 1.0f)
            };
            float yaw_fallback = extract_attr_float(line, "yaw", 0.0f);
            Vec3 rot = {
                extract_attr_float(line, "rx", 0.0f),
                extract_attr_float(line, "ry", yaw_fallback),
                extract_attr_float(line, "rz", 0.0f)
            };
            unsigned char r = clamp_u8((float)extract_attr_int(line, "r", 160));
            unsigned char g = clamp_u8((float)extract_attr_int(line, "g", 160));
            unsigned char b = clamp_u8((float)extract_attr_int(line, "b", 160));

            scene_add_box(s, c, sz, rot, r, g, b);
        }
    }

    fclose(f);
    if (s->light_count == 0) {
        scene_init_defaults(s);
    }
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

static void raster_tri(
    Vec3 p0, Vec3 p1, Vec3 p2,
    const Camera *cam,
    unsigned char r, unsigned char g, unsigned char b,
    const Light *lights, int light_count
) {
    float z0 = p0.z + cam->pos.z;
    float z1 = p1.z + cam->pos.z;
    float z2 = p2.z + cam->pos.z;
    if (!isfinite(z0) || !isfinite(z1) || !isfinite(z2)) return;
    if (z0 <= 0.25f || z1 <= 0.25f || z2 <= 0.25f) return;

    Vec3 n = v3_norm(v3_cross(v3_sub(p1, p0), v3_sub(p2, p0)));
    Vec3 centroid = {
        (p0.x + p1.x + p2.x) / 3.0f,
        (p0.y + p1.y + p2.y) / 3.0f,
        (p0.z + p1.z + p2.z) / 3.0f
    };

    float acc_r = 0.16f;
    float acc_g = 0.17f;
    float acc_b = 0.19f;

    for (int i = 0; i < light_count; i++) {
        Vec3 ldir = v3_norm(v3_sub(lights[i].pos, centroid));
        float ndotl = v3_dot(n, ldir);
        if (ndotl < 0.0f) ndotl = 0.0f;
        float diff = ndotl * lights[i].intensity;
        acc_r += diff * lights[i].color.x;
        acc_g += diff * lights[i].color.y;
        acc_b += diff * lights[i].color.z;
    }

    if (acc_r > 1.0f) acc_r = 1.0f;
    if (acc_g > 1.0f) acc_g = 1.0f;
    if (acc_b > 1.0f) acc_b = 1.0f;

    unsigned char sr = clamp_u8((float)r * acc_r);
    unsigned char sg = clamp_u8((float)g * acc_g);
    unsigned char sb = clamp_u8((float)b * acc_b);

    float sx0 = W * 0.5f + ((p0.x - cam->pos.x) * cam->fov) / z0;
    float sy0 = H * 0.5f - ((p0.y - cam->pos.y) * cam->fov) / z0;
    float sx1 = W * 0.5f + ((p1.x - cam->pos.x) * cam->fov) / z1;
    float sy1 = H * 0.5f - ((p1.y - cam->pos.y) * cam->fov) / z1;
    float sx2 = W * 0.5f + ((p2.x - cam->pos.x) * cam->fov) / z2;
    float sy2 = H * 0.5f - ((p2.y - cam->pos.y) * cam->fov) / z2;

    if (!isfinite(sx0) || !isfinite(sy0) || !isfinite(sx1) || !isfinite(sy1) || !isfinite(sx2) || !isfinite(sy2)) {
        return;
    }

    float area = edge_fn(sx0, sy0, sx1, sy1, sx2, sy2);
    if (!isfinite(area) || fabsf(area) < 1e-4f) return;

    int min_x = (int)floorf(fminf(sx0, fminf(sx1, sx2)));
    int max_x = (int)ceilf(fmaxf(sx0, fmaxf(sx1, sx2)));
    int min_y = (int)floorf(fminf(sy0, fminf(sy1, sy2)));
    int max_y = (int)ceilf(fmaxf(sy0, fmaxf(sy1, sy2)));

    if (min_x < 0) min_x = 0;
    if (max_x >= W) max_x = W - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= H) max_y = H - 1;

    float inv_area = 1.0f / area;

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

            w0 *= inv_area;
            w1 *= inv_area;
            w2 *= inv_area;

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

static int save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", path, strerror(errno));
        return 0;
    }
    if (fprintf(f, "P6\n%d %d\n255\n", W, H) < 0) {
        fclose(f);
        return 0;
    }
    size_t written = fwrite(fb, 1, sizeof(fb), f);
    if (fclose(f) != 0 || written != sizeof(fb)) {
        fprintf(stderr, "Error: incomplete write to '%s'\n", path);
        return 0;
    }
    return 1;
}

static int render_frame(const Scene *s, float orbit_yaw, const char *out_path) {
    clear_buffers(18, 24, 34);

    Camera frame_cam = s->cam;
    frame_cam.yaw += orbit_yaw;

    for (int i = 0; i < s->tcount; i++) {
        Tri t = s->tris[i];
        if (t.v0 < 0 || t.v0 >= s->vcount || t.v1 < 0 || t.v1 >= s->vcount || t.v2 < 0 || t.v2 >= s->vcount) {
            continue;
        }
        Vec3 p0 = rot_xyz(s->verts[t.v0], frame_cam.pitch, frame_cam.yaw, 0.0f);
        Vec3 p1 = rot_xyz(s->verts[t.v1], frame_cam.pitch, frame_cam.yaw, 0.0f);
        Vec3 p2 = rot_xyz(s->verts[t.v2], frame_cam.pitch, frame_cam.yaw, 0.0f);
        raster_tri(p0, p1, p2, &frame_cam, t.r, t.g, t.b, s->lights, s->light_count);
    }

    return save_ppm(out_path);
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
    if (scene_path && load_scene_xml(&s, scene_path)) {
        printf("Loaded XML scene '%s': %d objs, %d lights, %d verts, %d tris\n",
               scene_path, s.obj_count, s.light_count, s.vcount, s.tcount);
    } else {
        build_default_stress_scene(&s);
        printf("Built default stress scene: %d objs, %d lights, %d verts, %d tris\n",
               s.obj_count, s.light_count, s.vcount, s.tcount);
    }

    int ok_frames = 0;
    double batch_start = now_sec();

    for (int i = 0; i < count; i++) {
        int f = start_frame + i;
        double f_start = now_sec();
        float orbit = ((float)(f % total_cycle) / (float)total_cycle) * 6.2831853f;

        char path[512];
        int n = snprintf(path, sizeof(path), "%s/frame_%04d.ppm", out_dir, f);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            fprintf(stderr, "Error: output path too long for frame %d\n", f);
            return 1;
        }

        if (!render_frame(&s, orbit, path)) {
            return 1;
        }

        ok_frames++;
        double dt = now_sec() - f_start;
        printf("Frame %04d -> %s (%.3fs)\n", f, path, dt);
        fflush(stdout);
    }

    double total_dt = now_sec() - batch_start;
    double avg_dt = (ok_frames > 0) ? (total_dt / (double)ok_frames) : 0.0;
    printf("Batch finished: %d/%d frames in %.3fs (avg %.3fs/frame)\n",
           ok_frames, count, total_dt, avg_dt);
    return 0;
}

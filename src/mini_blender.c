#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define W 640
#define H 480
#define PPM_MIN_BYTES (W * H * 3)
#define MAX_V 131072
#define MAX_T 262144
#define MAX_OBJS 1024
#define MAX_LIGHTS 8
#define MAX_CUSTOM_V 8192
#define MAX_CUSTOM_T 16384
#define MAX_TAG_BUF 4096
#define MAX_BATCH_FRAMES 500000
#define MAX_CYCLE_FRAMES 1000000

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int v0, v1, v2;
    unsigned char r, g, b;
    float spec;
    float shine;
    float emit;
} Tri;

typedef struct {
    int v_start, v_count;
    int t_start, t_count;
    Vec3 center;
    float spin_y;
    int visible;
} ObjectSpan;

typedef struct {
    Vec3 pos;
    Vec3 color;
    float intensity;
} Light;

typedef struct {
    Vec3 pos;
    Vec3 target;
    int has_target;
    float yaw;
    float pitch;
    float roll;
    float fov;
    int orbit_cam;
} Camera;

typedef struct {
    unsigned char bg_r, bg_g, bg_b;
    float ambient;
} World;

typedef struct {
    Vec3 verts[MAX_V];
    int vcount;
    Tri tris[MAX_T];
    int tcount;
    ObjectSpan objs[MAX_OBJS];
    int obj_count;
    Camera cam;
    World world;
    Light lights[MAX_LIGHTS];
    int light_count;
} Scene;

static unsigned char fb[H][W][3];
static float zbuf[H][W];
static Scene g_scene;

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

static float clamp_range(float v, float lo, float hi, float def_val) {
    if (!isfinite(v)) return def_val;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


static float mb_finf(float v, float fallback) {
    return isfinite(v) ? v : fallback;
}

static Vec3 mb_vec3_sane(Vec3 v, Vec3 fallback) {
    Vec3 out;
    out.x = mb_finf(v.x, fallback.x);
    out.y = mb_finf(v.y, fallback.y);
    out.z = mb_finf(v.z, fallback.z);
    return out;
}

static void mb_cam_apply_target_and_roll(Camera *cam) {
    if (!cam) return;
    Vec3 zero = {0.0f, 0.0f, 0.0f};
    cam->pos = mb_vec3_sane(cam->pos, zero);
    cam->target = mb_vec3_sane(cam->target, zero);
    cam->roll = mb_finf(cam->roll, 0.0f);
    cam->yaw = mb_finf(cam->yaw, 0.0f);
    cam->pitch = mb_finf(cam->pitch, 0.0f);
    cam->fov = mb_finf(cam->fov, 420.0f);

    if (cam->has_target) {
        float dx = cam->target.x - cam->pos.x;
        float dy = cam->target.y - cam->pos.y;
        float dz = cam->target.z - cam->pos.z;
        float horiz = sqrtf(dx * dx + dz * dz);
        if (horiz > 1e-6f || fabsf(dy) > 1e-6f) {
            float yaw = atan2f(dx, (fabsf(dz) > 1e-6f ? dz : 1e-6f));
            float pitch = -atan2f(dy, (horiz > 1e-6f ? horiz : 1e-6f));
            cam->yaw = clamp_range(yaw, -50.0f, 50.0f, cam->yaw);
            cam->pitch = clamp_range(pitch, -50.0f, 50.0f, cam->pitch);
        }
    }
}

static void mb_scene_sanitize_transforms(Scene *s) {
    if (!s) return;
    Vec3 zero = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < s->light_count; i++) {
        Light *l = &s->lights[i];
        l->pos = mb_vec3_sane(l->pos, zero);
        l->intensity = mb_finf(l->intensity, 1.0f);
        if (l->intensity < 0.0f) l->intensity = 0.0f;
    }
}

static float clamp_scene_scale(float s) {
    return clamp_range(s, 0.1f, 10.0f, 1.0f);
}

static int has_control_chars(const char *s) {
    if (!s) return 1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 && *p != '\t') return 1;
    }
    return 0;
}

static int has_path_traversal(const char *path) {
    if (!path) return 1;
    if (strstr(path, "..") != NULL) return 1;
    if (strchr(path, '\\') != NULL) return 1;
    if (strstr(path, "//") != NULL) return 1;
    return 0;
}

static int is_safe_out_dir(const char *path) {
    if (!path || path[0] == '\0') return 0;
    if (strlen(path) > 256) return 0;
    if (has_control_chars(path)) return 0;
    if (has_path_traversal(path)) return 0;
    return 1;
}

static int is_safe_xml_path(const char *path) {
    if (!path || path[0] == '\0') return 0;
    if (strlen(path) > 400) return 0;
    if (has_control_chars(path)) return 0;
    if (has_path_traversal(path)) return 0;
    return 1;
}

static void sanitize_shell_line(char *line) {
    if (!line) return;
    size_t w_idx = 0;
    for (size_t r_idx = 0; line[r_idx] != '\0'; r_idx++) {
        unsigned char c = (unsigned char)line[r_idx];
        if (c == '\r' || c == '\n') continue;
        if (c < 0x20 && c != '\t') continue;
        line[w_idx++] = (char)c;
    }
    line[w_idx] = '\0';
}

static void sanitize_token(const char *src, char *dst, size_t dst_sz, const char *fallback) {
    if (!dst || dst_sz == 0) return;
    snprintf(dst, dst_sz, "%s", fallback ? fallback : "box");
    if (!src || src[0] == '\0') return;
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '_' || c == '-') {
            dst[j++] = (char)c;
        }
    }
    if (j > 0) {
        dst[j] = '\0';
    }
}

static int validate_batch_limits(int *start_frame, int *count, int *total_cycle) {
    if (!start_frame || !count || !total_cycle) return 0;
    if (*start_frame < 0) *start_frame = 0;
    if (*start_frame > 10000000) return 0;
    if (*count < 1 || *count > MAX_BATCH_FRAMES) return 0;
    if (*total_cycle < 1 || *total_cycle > MAX_CYCLE_FRAMES) return 0;
    return 1;
}

static int parse_float_safe(const char *s, float *out) {
    if (!s || !out || has_control_chars(s)) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return 0;
    errno = 0;
    char *endptr = NULL;
    float val = strtof(s, &endptr);
    if (s == endptr || errno == ERANGE || !isfinite(val)) return 0;
    *out = val;
    return 1;
}

static int parse_int_safe(const char *s, int *out) {
    if (!s || !out || has_control_chars(s)) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return 0;
    errno = 0;
    char *endptr = NULL;
    long val = strtol(s, &endptr, 10);
    if (s == endptr || errno == ERANGE) return 0;
    if (val < -100000000L || val > 100000000L) return 0;
    *out = (int)val;
    return 1;
}

static int parse_vec3_csv(const char *s, Vec3 *out) {
    if (!s || !out || has_control_chars(s)) return 0;
    float a = 0.0f, b = 0.0f, c = 0.0f;
    if (sscanf(s, " %f , %f , %f ", &a, &b, &c) != 3) return 0;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c)) return 0;
    if (fabsf(a) > 1e6f || fabsf(b) > 1e6f || fabsf(c) > 1e6f) return 0;
    out->x = a;
    out->y = b;
    out->z = c;
    return 1;
}

static int validate_tri_indices(int a, int b, int c, int vcount) {
    if (vcount <= 0) return 0;
    if (a < 0 || a >= vcount || b < 0 || b >= vcount || c < 0 || c >= vcount) return 0;
    if (a == b || b == c || a == c) return 0;
    return 1;
}

static void reset_mesh_accumulator(int *in_mesh_obj, int *cv_count, int *ct_count) {
    if (in_mesh_obj) *in_mesh_obj = 0;
    if (cv_count) *cv_count = 0;
    if (ct_count) *ct_count = 0;
}

static int ensure_dir_recursive(const char *dir_path) {
    if (!dir_path || dir_path[0] == '\0') return 0;
    char tmp[512];
    size_t len = strlen(dir_path);
    if (len == 0 || len >= sizeof(tmp)) return 0;
    memcpy(tmp, dir_path, len + 1);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (tmp[0] != '\0' && mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return 0;
            }
            tmp[i] = saved;
        }
    }
    return 1;
}

static int frame_already_complete(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    return (st.st_size >= PPM_MIN_BYTES) ? 1 : 0;
}

static void write_checkpoint(const char *out_dir, int last_frame, int rendered_count, int skipped_count) {
    char ck_path[560];
    int n = snprintf(ck_path, sizeof(ck_path), "%s/checkpoint.state", out_dir);
    if (n <= 0 || (size_t)n >= sizeof(ck_path)) return;
    FILE *f = fopen(ck_path, "w");
    if (!f) return;
    fprintf(f, "last_frame=%d\nrendered=%d\nskipped=%d\n", last_frame, rendered_count, skipped_count);
    fclose(f);
}

static Vec3 v3_sub(Vec3 a, Vec3 b) {
    Vec3 r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

static Vec3 v3_add(Vec3 a, Vec3 b) {
    Vec3 r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}

static Vec3 v3_scale(Vec3 a, float s) {
    Vec3 r = { a.x * s, a.y * s, a.z * s };
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
    s->cam.pos.y = 2.0f;
    s->cam.pos.z = 12.5f;
    s->cam.yaw = 0.0f;
    s->cam.pitch = -0.16f;
    s->cam.fov = 420.0f;
    s->cam.target.x = 0.0f;
    s->cam.target.y = 0.0f;
    s->cam.target.z = 0.0f;
    s->cam.has_target = 0;
    s->cam.roll = 0.0f;
    s->cam.orbit_cam = 0;

    s->world.bg_r = 18;
    s->world.bg_g = 24;
    s->world.bg_b = 34;
    s->world.ambient = 0.16f;

    s->lights[0].pos.x = 6.5f;
    s->lights[0].pos.y = 9.5f;
    s->lights[0].pos.z = 7.5f;
    s->lights[0].color.x = 1.0f;
    s->lights[0].color.y = 0.96f;
    s->lights[0].color.z = 0.88f;
    s->lights[0].intensity = 0.85f;

    s->lights[1].pos.x = -7.0f;
    s->lights[1].pos.y = 4.5f;
    s->lights[1].pos.z = -5.0f;
    s->lights[1].color.x = 0.55f;
    s->lights[1].color.y = 0.72f;
    s->lights[1].color.z = 0.95f;
    s->lights[1].intensity = 0.35f;

    s->light_count = 2;
}

static int can_append_scene_capacity(const Scene *s, int add_v, int add_t) {
    if (!s || add_v <= 0 || add_t <= 0) return 0;
    if (s->obj_count >= MAX_OBJS) return 0;
    if (s->vcount < 0 || s->tcount < 0) return 0;
    if (add_v > MAX_V - s->vcount || add_t > MAX_T - s->tcount) return 0;
    return 1;
}

static int scene_add_box(
    Scene *s, Vec3 center, Vec3 size, Vec3 rot_angles,
    unsigned char r, unsigned char g, unsigned char b,
    float spec, float shine, float emit, float spin_y, int visible
) {
    if (!can_append_scene_capacity(s, 8, 12)) return 0;
    int v_start = s->vcount;
    int t_start = s->tcount;

    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;

    Vec3 local[8] = {
        { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
        { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
    };

    for (int i = 0; i < 8; i++) {
        Vec3 rot = rot_xyz(local[i], rot_angles.x, rot_angles.y, rot_angles.z);
        s->verts[s->vcount++] = v3_add(center, rot);
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
        Tri t = { v_start + idx[i][0], v_start + idx[i][1], v_start + idx[i][2], r, g, b, spec, shine, emit };
        s->tris[s->tcount++] = t;
    }

    ObjectSpan sp = { v_start, 8, t_start, 12, center, spin_y, visible ? 1 : 0 };
    s->objs[s->obj_count++] = sp;
    return 1;
}

static int scene_add_plane(
    Scene *s, Vec3 center, float sx, float sz, Vec3 rot_angles,
    unsigned char r, unsigned char g, unsigned char b,
    float spec, float shine, float emit, float spin_y, int visible
) {
    if (!can_append_scene_capacity(s, 4, 2)) return 0;
    int v_start = s->vcount;
    int t_start = s->tcount;

    float hx = sx * 0.5f;
    float hz = sz * 0.5f;
    Vec3 local[4] = {
        { -hx, 0.0f, -hz },
        {  hx, 0.0f, -hz },
        {  hx, 0.0f,  hz },
        { -hx, 0.0f,  hz }
    };

    for (int i = 0; i < 4; i++) {
        Vec3 rot = rot_xyz(local[i], rot_angles.x, rot_angles.y, rot_angles.z);
        s->verts[s->vcount++] = v3_add(center, rot);
    }

    Tri t0 = { v_start + 0, v_start + 2, v_start + 1, r, g, b, spec, shine, emit };
    Tri t1 = { v_start + 0, v_start + 3, v_start + 2, r, g, b, spec, shine, emit };
    s->tris[s->tcount++] = t0;
    s->tris[s->tcount++] = t1;

    ObjectSpan sp = { v_start, 4, t_start, 2, center, spin_y, visible ? 1 : 0 };
    s->objs[s->obj_count++] = sp;
    return 1;
}

static int scene_add_pyramid(
    Scene *s, Vec3 center, Vec3 size, Vec3 rot_angles,
    unsigned char r, unsigned char g, unsigned char b,
    float spec, float shine, float emit, float spin_y, int visible
) {
    if (!can_append_scene_capacity(s, 5, 6)) return 0;
    int v_start = s->vcount;
    int t_start = s->tcount;

    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;
    Vec3 local[5] = {
        { -hx, -hy, -hz },
        {  hx, -hy, -hz },
        {  hx, -hy,  hz },
        { -hx, -hy,  hz },
        { 0.0f,  hy, 0.0f }
    };

    for (int i = 0; i < 5; i++) {
        Vec3 rot = rot_xyz(local[i], rot_angles.x, rot_angles.y, rot_angles.z);
        s->verts[s->vcount++] = v3_add(center, rot);
    }

    static const int idx[6][3] = {
        {0, 2, 1}, {0, 3, 2},
        {0, 1, 4}, {1, 2, 4},
        {2, 3, 4}, {3, 0, 4}
    };

    for (int i = 0; i < 6; i++) {
        Tri t = { v_start + idx[i][0], v_start + idx[i][1], v_start + idx[i][2], r, g, b, spec, shine, emit };
        s->tris[s->tcount++] = t;
    }

    ObjectSpan sp = { v_start, 5, t_start, 6, center, spin_y, visible ? 1 : 0 };
    s->objs[s->obj_count++] = sp;
    return 1;
}

static int scene_add_custom_mesh(
    Scene *s, Vec3 center, Vec3 scale, Vec3 rot_angles,
    const Vec3 *cv, int cv_count, const int *ct, int ct_count,
    unsigned char r, unsigned char g, unsigned char b,
    float spec, float shine, float emit, float spin_y, int visible
) {
    if (!cv || !ct || cv_count <= 0 || ct_count <= 0) return 0;
    if (!can_append_scene_capacity(s, cv_count, ct_count)) return 0;

    int v_start = s->vcount;
    int t_start = s->tcount;

    for (int i = 0; i < cv_count; i++) {
        Vec3 scaled = { cv[i].x * scale.x, cv[i].y * scale.y, cv[i].z * scale.z };
        Vec3 rot = rot_xyz(scaled, rot_angles.x, rot_angles.y, rot_angles.z);
        s->verts[s->vcount++] = v3_add(center, rot);
    }

    for (int i = 0; i < ct_count; i++) {
        int a = ct[i * 3 + 0];
        int b_idx = ct[i * 3 + 1];
        int c_idx = ct[i * 3 + 2];
        if (!validate_tri_indices(a, b_idx, c_idx, cv_count)) {
            continue;
        }
        Tri t = { v_start + a, v_start + b_idx, v_start + c_idx, r, g, b, spec, shine, emit };
        s->tris[s->tcount++] = t;
    }

    int added_t = s->tcount - t_start;
    if (added_t <= 0) {
        s->vcount = v_start;
        return 0;
    }

    ObjectSpan sp = { v_start, cv_count, t_start, added_t, center, spin_y, visible ? 1 : 0 };
    s->objs[s->obj_count++] = sp;
    return 1;
}

static void scene_add_grid_boxes(
    Scene *s, Vec3 origin, Vec3 box_sz, int rows, int cols, float step,
    unsigned char r, unsigned char g, unsigned char b,
    float spec, float shine, float emit, float spin_y, int visible
) {
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    if (step < 0.05f) step = 0.5f;

    float half_x = ((float)(cols - 1) * step) * 0.5f;
    float half_z = ((float)(rows - 1) * step) * 0.5f;

    for (int rz = 0; rz < rows; rz++) {
        for (int cx = 0; cx < cols; cx++) {
            float wave = 0.45f + 0.55f * fabsf(sinf((float)cx * 0.65f + (float)rz * 0.55f));
            float h = box_sz.y * wave;
            Vec3 c = {
                origin.x - half_x + (float)cx * step,
                origin.y + h * 0.5f,
                origin.z - half_z + (float)rz * step
            };
            Vec3 sz = { box_sz.x, h, box_sz.z };
            Vec3 rot = { 0.0f, 0.12f * (float)((cx + rz) % 6), 0.0f };
            unsigned char cr = clamp_u8((float)r + (float)((cx * 13 + rz * 7) % 35) - 15.0f);
            unsigned char cg = clamp_u8((float)g + (float)((cx * 5 + rz * 19) % 35) - 15.0f);
            unsigned char cb = clamp_u8((float)b + (float)((cx * 11 + rz * 17) % 35) - 15.0f);
            if (!scene_add_box(s, c, sz, rot, cr, cg, cb, spec, shine, emit, spin_y, visible)) {
                return;
            }
        }
    }
}

static void build_default_stress_scene(Scene *s) {
    scene_init_defaults(s);
    s->cam.orbit_cam = 1;

    Vec3 zero_r = { 0.0f, 0.0f, 0.0f };
    Vec3 ground_c = { 0.0f, -1.3f, 0.0f };
    scene_add_plane(s, ground_c, 20.0f, 20.0f, zero_r, 68, 78, 92, 0.10f, 20.0f, 0.0f, 0.0f, 1);

    Vec3 grid_o = { 0.0f, -1.3f, 0.0f };
    Vec3 box_sz = { 0.56f, 0.85f, 0.56f };
    scene_add_grid_boxes(s, grid_o, box_sz, 14, 14, 1.0f, 110, 145, 185, 0.32f, 24.0f, 0.0f, 0.0f, 1);
}

static const char *find_attr_value(const char *tag, const char *key, char *val_buf, size_t val_sz) {
    if (!tag || !key || !val_buf || val_sz == 0) return NULL;
    size_t klen = strlen(key);
    const char *p = tag;
    while ((p = strstr(p, key)) != NULL) {
        int left_ok = (p == tag) || isspace((unsigned char)p[-1]) || p[-1] == '<';
        const char *after_key = p + klen;
        while (*after_key && isspace((unsigned char)*after_key)) after_key++;
        if (left_ok && *after_key == '=') {
            const char *q = after_key + 1;
            while (*q && isspace((unsigned char)*q)) q++;
            char quote = *q;
            if (quote == '"' || quote == '\'') {
                q++;
                const char *endq = strchr(q, quote);
                if (!endq) return NULL;
                size_t len = (size_t)(endq - q);
                if (len >= val_sz) len = val_sz - 1;
                memcpy(val_buf, q, len);
                val_buf[len] = '\0';
                return val_buf;
            }
        }
        p += klen;
    }
    return NULL;
}

static float attr_float(const char *tag, const char *key, float def_val) {
    char buf[128];
    if (!find_attr_value(tag, key, buf, sizeof(buf))) return def_val;
    float v = def_val;
    if (!parse_float_safe(buf, &v)) return def_val;
    return v;
}

static int attr_int(const char *tag, const char *key, int def_val) {
    char buf[128];
    if (!find_attr_value(tag, key, buf, sizeof(buf))) return def_val;
    int v = def_val;
    if (!parse_int_safe(buf, &v)) return def_val;
    return v;
}

static unsigned char attr_color_u8(const char *tag, const char *key, unsigned char def_val) {
    char buf[128];
    if (!find_attr_value(tag, key, buf, sizeof(buf))) return def_val;
    float fv = 0.0f;
    if (!parse_float_safe(buf, &fv)) return def_val;
    if (strchr(buf, '.') != NULL && fv >= 0.0f && fv <= 1.0f) {
        return clamp_u8(fv * 255.0f);
    }
    return clamp_u8(fv);
}

static void attr_str(const char *tag, const char *key, char *dst, size_t dst_sz, const char *def_val) {
    char buf[128];
    if (!find_attr_value(tag, key, buf, sizeof(buf))) {
        snprintf(dst, dst_sz, "%s", def_val);
        return;
    }
    sanitize_token(buf, dst, dst_sz, def_val);
}

static void apply_optional_vec3_attr(const char *tag, const char *key, Vec3 *target) {
    char buf[128];
    Vec3 tmp;
    if (find_attr_value(tag, key, buf, sizeof(buf)) && parse_vec3_csv(buf, &tmp)) {
        *target = tmp;
    }
}

static void apply_optional_rgb_attr(const char *tag, const char *key, unsigned char *r, unsigned char *g, unsigned char *b) {
    char buf[128];
    Vec3 tmp;
    if (!find_attr_value(tag, key, buf, sizeof(buf)) || !parse_vec3_csv(buf, &tmp)) return;
    int norm = (strchr(buf, '.') != NULL && tmp.x >= 0.0f && tmp.x <= 1.0f && tmp.y >= 0.0f && tmp.y <= 1.0f && tmp.z >= 0.0f && tmp.z <= 1.0f);
    *r = clamp_u8(norm ? tmp.x * 255.0f : tmp.x);
    *g = clamp_u8(norm ? tmp.y * 255.0f : tmp.y);
    *b = clamp_u8(norm ? tmp.z * 255.0f : tmp.z);
}

static void process_xml_tag(
    Scene *s, const char *tag,
    int *parsed_custom_light, int *in_mesh_obj,
    char *obj_type, size_t obj_type_sz,
    Vec3 *obj_c, Vec3 *obj_s, Vec3 *obj_r,
    unsigned char obj_col[3],
    float *obj_spec, float *obj_shine, float *obj_emit, float *obj_spin,
    int *obj_visible, int *obj_rows, int *obj_cols, float *obj_step,
    Vec3 *cv, int *cv_count, int *ct, int *ct_count
) {
    if (strncmp(tag, "<world", 6) == 0 && (tag[6] == '\0' || isspace((unsigned char)tag[6]) || tag[6] == '/' || tag[6] == '>')) {
        s->world.bg_r = attr_color_u8(tag, "r", s->world.bg_r);
        s->world.bg_g = attr_color_u8(tag, "g", s->world.bg_g);
        s->world.bg_b = attr_color_u8(tag, "b", s->world.bg_b);
        apply_optional_rgb_attr(tag, "color", &s->world.bg_r, &s->world.bg_g, &s->world.bg_b);
        s->world.ambient = clamp_range(attr_float(tag, "ambient", s->world.ambient), 0.0f, 1.0f, s->world.ambient);
    } else if (strncmp(tag, "<camera", 7) == 0 && (tag[7] == '\0' || isspace((unsigned char)tag[7]) || tag[7] == '/' || tag[7] == '>')) {
        s->cam.pos.x = clamp_range(attr_float(tag, "x", s->cam.pos.x), -1e5f, 1e5f, s->cam.pos.x);
        s->cam.pos.y = clamp_range(attr_float(tag, "y", s->cam.pos.y), -1e5f, 1e5f, s->cam.pos.y);
        s->cam.pos.z = clamp_range(attr_float(tag, "z", s->cam.pos.z), -1e5f, 1e5f, s->cam.pos.z);
        apply_optional_vec3_attr(tag, "location", &s->cam.pos);
        Vec3 tgt_tmp;
        char tgt_buf[128];
        if (find_attr_value(tag, "target", tgt_buf, sizeof(tgt_buf)) && parse_vec3_csv(tgt_buf, &tgt_tmp)) {
            s->cam.target = tgt_tmp;
            s->cam.has_target = 1;
            float dx = s->cam.target.x - s->cam.pos.x;
            float dy = s->cam.target.y - s->cam.pos.y;
            float dz = s->cam.target.z - s->cam.pos.z;
            float horiz = sqrtf(dx * dx + dz * dz);
            if (horiz > 1e-6f || fabsf(dy) > 1e-6f) {
                s->cam.yaw = clamp_range(atan2f(dx, (fabsf(dz) > 1e-6f ? dz : 1e-6f)), -50.0f, 50.0f, s->cam.yaw);
                s->cam.pitch = clamp_range(-atan2f(dy, (horiz > 1e-6f ? horiz : 1e-6f)), -50.0f, 50.0f, s->cam.pitch);
            }
        }
        s->cam.roll = clamp_range(attr_float(tag, "roll", s->cam.roll), -50.0f, 50.0f, s->cam.roll);
        mb_cam_apply_target_and_roll(&s->cam);
        mb_scene_sanitize_transforms(s);
        s->cam.yaw = clamp_range(attr_float(tag, "yaw", s->cam.yaw), -50.0f, 50.0f, s->cam.yaw);
        s->cam.pitch = clamp_range(attr_float(tag, "pitch", s->cam.pitch), -50.0f, 50.0f, s->cam.pitch);
        s->cam.fov = clamp_range(attr_float(tag, "fov", s->cam.fov), 50.0f, 1400.0f, 420.0f);
        s->cam.orbit_cam = attr_int(tag, "orbit_cam", s->cam.orbit_cam) ? 1 : 0;
    } else if (strncmp(tag, "<light", 6) == 0 && (tag[6] == '\0' || isspace((unsigned char)tag[6]) || tag[6] == '/' || tag[6] == '>')) {
        if (!(*parsed_custom_light)) {
            s->light_count = 0;
            *parsed_custom_light = 1;
        }
        if (s->light_count < MAX_LIGHTS) {
            Light lt;
            lt.pos.x = clamp_range(attr_float(tag, "x", 5.0f), -1e5f, 1e5f, 5.0f);
            lt.pos.y = clamp_range(attr_float(tag, "y", 8.0f), -1e5f, 1e5f, 8.0f);
            lt.pos.z = clamp_range(attr_float(tag, "z", 6.0f), -1e5f, 1e5f, 6.0f);
            apply_optional_vec3_attr(tag, "location", &lt.pos);
            unsigned char lr = attr_color_u8(tag, "r", 255);
            unsigned char lg = attr_color_u8(tag, "g", 255);
            unsigned char lb = attr_color_u8(tag, "b", 255);
            apply_optional_rgb_attr(tag, "color", &lr, &lg, &lb);
            lt.color.x = (float)lr / 255.0f;
            lt.color.y = (float)lg / 255.0f;
            lt.color.z = (float)lb / 255.0f;
            lt.intensity = clamp_range(attr_float(tag, "intensity", 0.8f), 0.0f, 4.0f, 0.8f);
            s->lights[s->light_count++] = lt;
        }
    } else if (strncmp(tag, "<object", 7) == 0 && (tag[7] == '\0' || isspace((unsigned char)tag[7]) || tag[7] == '/' || tag[7] == '>')) {
        attr_str(tag, "type", obj_type, obj_type_sz, "box");
        obj_c->x = clamp_range(attr_float(tag, "x", 0.0f), -1e5f, 1e5f, 0.0f);
        obj_c->y = clamp_range(attr_float(tag, "y", 0.0f), -1e5f, 1e5f, 0.0f);
        obj_c->z = clamp_range(attr_float(tag, "z", 0.0f), -1e5f, 1e5f, 0.0f);
        apply_optional_vec3_attr(tag, "location", obj_c);

        obj_s->x = clamp_range(attr_float(tag, "sx", 1.0f), 0.01f, 200.0f, 1.0f);
        obj_s->y = clamp_range(attr_float(tag, "sy", 1.0f), 0.01f, 200.0f, 1.0f);
        obj_s->z = clamp_range(attr_float(tag, "sz", 1.0f), 0.01f, 200.0f, 1.0f);
        Vec3 sc_tmp = *obj_s;
        apply_optional_vec3_attr(tag, "scale", &sc_tmp);
        obj_s->x = clamp_range(sc_tmp.x, 0.01f, 200.0f, obj_s->x);
        obj_s->y = clamp_range(sc_tmp.y, 0.01f, 200.0f, obj_s->y);
        obj_s->z = clamp_range(sc_tmp.z, 0.01f, 200.0f, obj_s->z);

        float yaw_fb = attr_float(tag, "yaw", 0.0f);
        obj_r->x = clamp_range(attr_float(tag, "rx", 0.0f), -50.0f, 50.0f, 0.0f);
        obj_r->y = clamp_range(attr_float(tag, "ry", yaw_fb), -50.0f, 50.0f, yaw_fb);
        obj_r->z = clamp_range(attr_float(tag, "rz", 0.0f), -50.0f, 50.0f, 0.0f);
        apply_optional_vec3_attr(tag, "rotation", obj_r);

        obj_col[0] = attr_color_u8(tag, "r", 160);
        obj_col[1] = attr_color_u8(tag, "g", 160);
        obj_col[2] = attr_color_u8(tag, "b", 160);
        apply_optional_rgb_attr(tag, "color", &obj_col[0], &obj_col[1], &obj_col[2]);

        *obj_spec = clamp_range(attr_float(tag, "spec", 0.25f), 0.0f, 1.0f, 0.25f);
        *obj_shine = clamp_range(attr_float(tag, "shine", 24.0f), 2.0f, 256.0f, 24.0f);
        *obj_emit = clamp_range(attr_float(tag, "emit", 0.0f), 0.0f, 1.0f, 0.0f);
        *obj_spin = clamp_range(attr_float(tag, "spin_y", 0.0f), -10.0f, 10.0f, 0.0f);
        *obj_visible = attr_int(tag, "visible", 1) ? 1 : 0;
        *obj_rows = (int)clamp_range((float)attr_int(tag, "rows", 10), 1.0f, 40.0f, 10.0f);
        *obj_cols = (int)clamp_range((float)attr_int(tag, "cols", 10), 1.0f, 40.0f, 10.0f);
        *obj_step = clamp_range(attr_float(tag, "step", 1.0f), 0.1f, 20.0f, 1.0f);

        int self_closing = (strstr(tag, "/>") != NULL);
        if (strcmp(obj_type, "mesh") == 0 && !self_closing) {
            *in_mesh_obj = 1;
            *cv_count = 0;
            *ct_count = 0;
        } else {
            reset_mesh_accumulator(in_mesh_obj, cv_count, ct_count);
            if (strcmp(obj_type, "plane") == 0) {
                scene_add_plane(s, *obj_c, obj_s->x, obj_s->z, *obj_r, obj_col[0], obj_col[1], obj_col[2], *obj_spec, *obj_shine, *obj_emit, *obj_spin, *obj_visible);
            } else if (strcmp(obj_type, "pyramid") == 0) {
                scene_add_pyramid(s, *obj_c, *obj_s, *obj_r, obj_col[0], obj_col[1], obj_col[2], *obj_spec, *obj_shine, *obj_emit, *obj_spin, *obj_visible);
            } else if (strcmp(obj_type, "grid") == 0) {
                scene_add_grid_boxes(s, *obj_c, *obj_s, *obj_rows, *obj_cols, *obj_step, obj_col[0], obj_col[1], obj_col[2], *obj_spec, *obj_shine, *obj_emit, *obj_spin, *obj_visible);
            } else {
                scene_add_box(s, *obj_c, *obj_s, *obj_r, obj_col[0], obj_col[1], obj_col[2], *obj_spec, *obj_shine, *obj_emit, *obj_spin, *obj_visible);
            }
        }
    } else if (*in_mesh_obj && strncmp(tag, "<v", 2) == 0 && (tag[2] == '\0' || isspace((unsigned char)tag[2]) || tag[2] == '/' || tag[2] == '>')) {
        if (*cv_count < MAX_CUSTOM_V) {
            cv[*cv_count].x = clamp_range(attr_float(tag, "x", 0.0f), -1e4f, 1e4f, 0.0f);
            cv[*cv_count].y = clamp_range(attr_float(tag, "y", 0.0f), -1e4f, 1e4f, 0.0f);
            cv[*cv_count].z = clamp_range(attr_float(tag, "z", 0.0f), -1e4f, 1e4f, 0.0f);
            (*cv_count)++;
        }
    } else if (*in_mesh_obj && strncmp(tag, "<tri", 4) == 0 && (tag[4] == '\0' || isspace((unsigned char)tag[4]) || tag[4] == '/' || tag[4] == '>')) {
        if (*ct_count < MAX_CUSTOM_T) {
            ct[(*ct_count) * 3 + 0] = attr_int(tag, "v0", -1);
            ct[(*ct_count) * 3 + 1] = attr_int(tag, "v1", -1);
            ct[(*ct_count) * 3 + 2] = attr_int(tag, "v2", -1);
            (*ct_count)++;
        }
    } else if (*in_mesh_obj && strncmp(tag, "</object", 8) == 0) {
        scene_add_custom_mesh(
            s, *obj_c, *obj_s, *obj_r, cv, *cv_count, ct, *ct_count,
            obj_col[0], obj_col[1], obj_col[2], *obj_spec, *obj_shine, *obj_emit, *obj_spin, *obj_visible
        );
        reset_mesh_accumulator(in_mesh_obj, cv_count, ct_count);
    }
}

static int load_scene_xml(Scene *s, const char *path) {
    if (!is_safe_xml_path(path)) {
        fprintf(stderr, "Warning: unsafe or invalid scene path '%s'\n", path ? path : "");
        return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Warning: cannot open scene file '%s': %s\n", path, strerror(errno));
        return 0;
    }

    scene_init_defaults(s);
    int parsed_custom_light = 0;
    int in_mesh_obj = 0;

    char obj_type[32] = "box";
    Vec3 obj_c = { 0.0f, 0.0f, 0.0f };
    Vec3 obj_s = { 1.0f, 1.0f, 1.0f };
    Vec3 obj_r = { 0.0f, 0.0f, 0.0f };
    unsigned char obj_col[3] = { 160, 160, 160 };
    float obj_spec = 0.25f;
    float obj_shine = 24.0f;
    float obj_emit = 0.0f;
    float obj_spin = 0.0f;
    int obj_visible = 1;
    int obj_rows = 10;
    int obj_cols = 10;
    float obj_step = 1.0f;

    Vec3 *cv = malloc(sizeof(Vec3) * MAX_CUSTOM_V);
    int *ct = malloc(sizeof(int) * MAX_CUSTOM_T * 3);
    char *tag_buf = malloc(MAX_TAG_BUF);
    if (!cv || !ct || !tag_buf) {
        free(cv);
        free(ct);
        free(tag_buf);
        fclose(f);
        return 0;
    }
    int cv_count = 0;
    int ct_count = 0;
    int in_tag = 0;
    size_t tag_len = 0;

    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '<') {
            in_tag = 1;
            tag_len = 0;
            tag_buf[tag_len++] = (char)ch;
        } else if (in_tag && ch == '>') {
            if (tag_len + 1 < MAX_TAG_BUF) {
                tag_buf[tag_len++] = (char)ch;
            }
            tag_buf[tag_len] = '\0';
            if (strncmp(tag_buf, "<!--", 4) != 0 && strncmp(tag_buf, "<?", 2) != 0) {
                process_xml_tag(
                    s, tag_buf,
                    &parsed_custom_light, &in_mesh_obj,
                    obj_type, sizeof(obj_type),
                    &obj_c, &obj_s, &obj_r,
                    obj_col,
                    &obj_spec, &obj_shine, &obj_emit, &obj_spin,
                    &obj_visible, &obj_rows, &obj_cols, &obj_step,
                    cv, &cv_count, ct, &ct_count
                );
            }
            in_tag = 0;
            tag_len = 0;
        } else if (in_tag) {
            if (tag_len + 1 < MAX_TAG_BUF) {
                tag_buf[tag_len++] = (char)ch;
            }
        }
    }

    if (in_mesh_obj && cv_count > 0 && ct_count > 0) {
        scene_add_custom_mesh(
            s, obj_c, obj_s, obj_r, cv, cv_count, ct, ct_count,
            obj_col[0], obj_col[1], obj_col[2], obj_spec, obj_shine, obj_emit, obj_spin, obj_visible
        );
    }

    free(cv);
    free(ct);
    free(tag_buf);
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
    const Tri *tri,
    const Light *lights, int light_count,
    float ambient
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

    float acc_r = ambient + tri->emit;
    float acc_g = ambient + tri->emit;
    float acc_b = ambient + tri->emit;

    for (int i = 0; i < light_count; i++) {
        Vec3 ldir = v3_norm(v3_sub(lights[i].pos, centroid));
        float ndotl = v3_dot(n, ldir);
        if (ndotl < 0.0f) ndotl = 0.0f;
        float diff = ndotl * lights[i].intensity;

        float spec_term = 0.0f;
        if (ndotl > 0.0f && tri->spec > 0.0f) {
            Vec3 view_dir = v3_norm(v3_sub(cam->pos, centroid));
            Vec3 half_v = v3_norm(v3_add(ldir, view_dir));
            float nh = v3_dot(n, half_v);
            if (nh > 0.0f) {
                float p = powf(nh, tri->shine);
                spec_term = p * tri->spec * lights[i].intensity;
            }
        }

        acc_r += (diff + spec_term) * lights[i].color.x;
        acc_g += (diff + spec_term) * lights[i].color.y;
        acc_b += (diff + spec_term) * lights[i].color.z;
    }

    if (acc_r > 1.25f) acc_r = 1.25f;
    if (acc_g > 1.25f) acc_g = 1.25f;
    if (acc_b > 1.25f) acc_b = 1.25f;

    unsigned char sr = clamp_u8((float)tri->r * acc_r);
    unsigned char sg = clamp_u8((float)tri->g * acc_g);
    unsigned char sb = clamp_u8((float)tri->b * acc_b);

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
    char tmp_path[560];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return 0;
    }

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", tmp_path, strerror(errno));
        return 0;
    }
    if (fprintf(f, "P6\n%d %d\n255\n", W, H) < 0) {
        fclose(f);
        remove(tmp_path);
        return 0;
    }
    size_t written = fwrite(fb, 1, sizeof(fb), f);
    if (fclose(f) != 0 || written != sizeof(fb)) {
        fprintf(stderr, "Error: incomplete write to '%s'\n", tmp_path);
        remove(tmp_path);
        return 0;
    }
    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "Error: cannot finalize '%s': %s\n", path, strerror(errno));
        remove(tmp_path);
        return 0;
    }
    return 1;
}

#ifndef MB_NO_PREVIEW
static void print_terminal_preview(void) {
    const int cols = 80;
    const int rows = 24;
    int step_x = W / cols;
    int step_y = H / rows;
    if (step_x < 1) step_x = 1;
    if (step_y < 1) step_y = 1;

    printf("\n--- Terminal Truecolor Preview (%dx%d) ---\n", cols, rows);
    for (int ty = 0; ty < rows; ty++) {
        int y0 = (ty * H) / rows;
        int y1 = y0 + step_y;
        if (y1 > H) y1 = H;
        for (int tx = 0; tx < cols; tx++) {
            int x0 = (tx * W) / cols;
            int x1 = x0 + step_x;
            if (x1 > W) x1 = W;

            unsigned long sum_r = 0, sum_g = 0, sum_b = 0, cnt = 0;
            for (int y = y0; y < y1; y++) {
                for (int x = x0; x < x1; x++) {
                    sum_r += fb[y][x][0];
                    sum_g += fb[y][x][1];
                    sum_b += fb[y][x][2];
                    cnt++;
                }
            }
            unsigned char r = (unsigned char)(cnt ? (sum_r / cnt) : 0);
            unsigned char g = (unsigned char)(cnt ? (sum_g / cnt) : 0);
            unsigned char b = (unsigned char)(cnt ? (sum_b / cnt) : 0);
            char glyph = ((r + g + b) > 120) ? '#' : (((r + g + b) > 55) ? '+' : '.');
            printf("\x1b[48;2;%u;%u;%um%c", r, g, b, glyph);
        }
        printf("\x1b[0m\n");
    }
    printf("------------------------------------------\n");
}
#endif

static int render_frame(
    const Scene *s, float phase,
    Vec3 grab_offset, Vec3 rot_offset, float scene_scale,
    const char *out_path
) {
    clear_buffers(s->world.bg_r, s->world.bg_g, s->world.bg_b);

    Camera frame_cam = s->cam;
    frame_cam.pos = v3_add(frame_cam.pos, grab_offset);
    frame_cam.yaw += rot_offset.y;
    frame_cam.pitch += rot_offset.x;
    if (s->cam.orbit_cam) {
        frame_cam.yaw += phase;
    }

    Vec3 *anim_v = malloc(sizeof(Vec3) * (size_t)(s->vcount > 0 ? s->vcount : 1));
    if (!anim_v) return 0;

    for (int i = 0; i < s->vcount; i++) {
        Vec3 v = v3_scale(s->verts[i], scene_scale);
        if (fabsf(rot_offset.z) > 1e-6f) {
            v = rot_xyz(v, 0.0f, 0.0f, rot_offset.z);
        }
        anim_v[i] = v;
    }

    for (int oi = 0; oi < s->obj_count; oi++) {
        ObjectSpan sp = s->objs[oi];
        if (!sp.visible) continue;
        if (fabsf(sp.spin_y) > 1e-5f) {
            Vec3 scaled_c = v3_scale(sp.center, scene_scale);
            float angle = phase * sp.spin_y;
            for (int vi = sp.v_start; vi < sp.v_start + sp.v_count && vi < s->vcount; vi++) {
                Vec3 rel = v3_sub(anim_v[vi], scaled_c);
                Vec3 rot = rot_xyz(rel, 0.0f, angle, 0.0f);
                anim_v[vi] = v3_add(scaled_c, rot);
            }
        }
    }

    Vec3 cam_shift = { -frame_cam.pos.x, -frame_cam.pos.y, 0.0f };
    Camera view_cam = frame_cam;
    view_cam.pos.x = 0.0f;
    view_cam.pos.y = 0.0f;

    for (int oi = 0; oi < s->obj_count; oi++) {
        ObjectSpan sp = s->objs[oi];
        if (!sp.visible) continue;
        int t_end = sp.t_start + sp.t_count;
        if (t_end > s->tcount) t_end = s->tcount;
        for (int i = sp.t_start; i < t_end; i++) {
            Tri t = s->tris[i];
            if (!validate_tri_indices(t.v0, t.v1, t.v2, s->vcount)) {
                continue;
            }
            Vec3 p0 = rot_xyz(v3_add(anim_v[t.v0], cam_shift), view_cam.pitch, view_cam.yaw, 0.0f);
            Vec3 p1 = rot_xyz(v3_add(anim_v[t.v1], cam_shift), view_cam.pitch, view_cam.yaw, 0.0f);
            Vec3 p2 = rot_xyz(v3_add(anim_v[t.v2], cam_shift), view_cam.pitch, view_cam.yaw, 0.0f);
            raster_tri(p0, p1, p2, &view_cam, &t, s->lights, s->light_count, s->world.ambient);
        }
    }

    free(anim_v);
    return save_ppm(out_path);
}

static int run_batch(
    int start_frame, int count, int total_cycle,
    const char *out_dir, const char *scene_path,
    Vec3 grab_offset, Vec3 rot_offset, float custom_fov, float scene_scale,
    int quiet, int preview, int resume_mode
) {
    if (!is_safe_out_dir(out_dir)) {
        fprintf(stderr, "Error: unsafe or invalid output directory '%s'\n", out_dir ? out_dir : "");
        return 1;
    }
    if (!validate_batch_limits(&start_frame, &count, &total_cycle)) {
        fprintf(stderr, "Error: batch parameters out of safe bounds (start=%d, count=%d, cycle=%d)\n",
                start_frame, count, total_cycle);
        return 1;
    }
    scene_scale = clamp_scene_scale(scene_scale);

    if (scene_path && load_scene_xml(&g_scene, scene_path)) {
        printf("Loaded XML scene '%s': %d objs, %d lights, %d verts, %d tris\n",
               scene_path, g_scene.obj_count, g_scene.light_count, g_scene.vcount, g_scene.tcount);
    } else {
        build_default_stress_scene(&g_scene);
        printf("Built default stress scene: %d objs, %d lights, %d verts, %d tris\n",
               g_scene.obj_count, g_scene.light_count, g_scene.vcount, g_scene.tcount);
    }

    if (custom_fov > 0.0f) {
        g_scene.cam.fov = clamp_range(custom_fov, 50.0f, 1400.0f, g_scene.cam.fov);
    }

    if (!ensure_dir_recursive(out_dir)) {
        fprintf(stderr, "Error: cannot create output directory '%s': %s\n", out_dir, strerror(errno));
        return 1;
    }

    int rendered_frames = 0;
    int skipped_frames = 0;
    double batch_start = now_sec();

    for (int i = 0; i < count; i++) {
        int f = start_frame + i;
        char path[512];
        int n = snprintf(path, sizeof(path), "%s/frame_%04d.ppm", out_dir, f);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            fprintf(stderr, "Error: output path too long for frame %d\n", f);
            return 1;
        }

        if (resume_mode && frame_already_complete(path)) {
            skipped_frames++;
            write_checkpoint(out_dir, f, rendered_frames, skipped_frames);
            continue;
        }

        double f_start = now_sec();
        float phase = ((float)(f % total_cycle) / (float)total_cycle) * 6.2831853f;

        if (!render_frame(&g_scene, phase, grab_offset, rot_offset, scene_scale, path)) {
            return 1;
        }

        rendered_frames++;
        write_checkpoint(out_dir, f, rendered_frames, skipped_frames);

        if (!quiet) {
            double dt = now_sec() - f_start;
            int done_total = rendered_frames + skipped_frames;
            double elapsed = now_sec() - batch_start;
            double avg = (rendered_frames > 0) ? (elapsed / (double)rendered_frames) : dt;
            double eta = avg * (double)(count - done_total);
            printf("Frame %04d -> %s (%.3fs | %d/%d | ETA %.1fs)\n",
                   f, path, dt, done_total, count, eta);
            fflush(stdout);
        }
    }

    double total_dt = now_sec() - batch_start;
    double avg_dt = (rendered_frames > 0) ? (total_dt / (double)rendered_frames) : 0.0;
    printf("Batch finished: rendered=%d skipped=%d total=%d in %.3fs (avg %.3fs/frame)\n",
           rendered_frames, skipped_frames, count, total_dt, avg_dt);

#ifndef MB_NO_PREVIEW
    if (preview) {
        print_terminal_preview();
    }
#else
    (void)preview;
#endif
    return 0;
}

static void print_shell_help(void) {
    printf(
        "Interactive Shell Commands (Blender-familiar):\n"
        "  g <x,y,z>      Grab / move offset\n"
        "  r <rx,ry,rz>   Rotate offset\n"
        "  z <fov>        Zoom / FOV\n"
        "  m <scale>      Scale multiplier\n"
        "  reset | alt    Reset g, r, z, m\n"
        "  1 | 3 | 7 | 0  Quick view presets (front/side/top/cam)\n"
        "  n <count>      Frame count\n"
        "  f <start>      Start frame\n"
        "  s <scene.xml>  Scene XML path\n"
        "  p              Toggle terminal preview\n"
        "  render | run   Run batch render\n"
        "  status         Print current settings\n"
        "  help           Show commands\n"
        "  quit | exit    Exit shell\n"
    );
}

static int run_interactive_shell(
    int start_frame, int count, int total_cycle,
    const char *out_dir, const char *scene_path,
    Vec3 grab_offset, Vec3 rot_offset, float custom_fov, float scene_scale,
    int preview, int resume_mode
) {
    char scene_buf[512] = "";
    if (scene_path && is_safe_xml_path(scene_path)) {
        snprintf(scene_buf, sizeof(scene_buf), "%s", scene_path);
    }

    printf("mini_blender interactive shell (type 'help' or 'render', 'quit' to exit)\n");
    char line[512];
    while (1) {
        printf("mb> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        sanitize_shell_line(line);
        if (line[0] == '\0') continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0 || strcmp(line, "q") == 0) {
            break;
        } else if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0) {
            print_shell_help();
        } else if (strcmp(line, "reset") == 0 || strcmp(line, "alt") == 0) {
            grab_offset.x = 0.0f; grab_offset.y = 0.0f; grab_offset.z = 0.0f;
            rot_offset.x = 0.0f; rot_offset.y = 0.0f; rot_offset.z = 0.0f;
            custom_fov = -1.0f;
            scene_scale = 1.0f;
            printf("Reset G/R/Z/M to defaults.\n");
        } else if (strcmp(line, "1") == 0) {
            rot_offset.x = 0.0f; rot_offset.y = 0.0f; rot_offset.z = 0.0f;
            printf("View preset: 1 (Front)\n");
        } else if (strcmp(line, "3") == 0) {
            rot_offset.x = 0.0f; rot_offset.y = 1.5707963f; rot_offset.z = 0.0f;
            printf("View preset: 3 (Side)\n");
        } else if (strcmp(line, "7") == 0) {
            rot_offset.x = -1.25f; rot_offset.y = 0.0f; rot_offset.z = 0.0f;
            printf("View preset: 7 (Top-ish)\n");
        } else if (strcmp(line, "0") == 0) {
            grab_offset.x = 0.0f; grab_offset.y = 0.0f; grab_offset.z = 0.0f;
            rot_offset.x = 0.0f; rot_offset.y = 0.0f; rot_offset.z = 0.0f;
            custom_fov = -1.0f;
            printf("View preset: 0 (Camera default)\n");
        } else if (strcmp(line, "p") == 0) {
            preview = !preview;
            printf("Terminal preview: %s\n", preview ? "ON" : "OFF");
        } else if (strcmp(line, "render") == 0 || strcmp(line, "run") == 0) {
            run_batch(start_frame, count, total_cycle, out_dir,
                      scene_buf[0] ? scene_buf : NULL,
                      grab_offset, rot_offset, custom_fov, scene_scale, 0, preview, resume_mode);
        } else if (strcmp(line, "status") == 0) {
            printf("f=%d n=%d c=%d o=%s scene=%s g=(%.2f,%.2f,%.2f) r=(%.2f,%.2f,%.2f) z=%.1f m=%.2f preview=%s resume=%d\n",
                   start_frame, count, total_cycle, out_dir,
                   scene_buf[0] ? scene_buf : "<default>",
                   grab_offset.x, grab_offset.y, grab_offset.z,
                   rot_offset.x, rot_offset.y, rot_offset.z,
                   custom_fov, scene_scale, preview ? "ON" : "OFF", resume_mode);
        } else if (strncmp(line, "g ", 2) == 0) {
            if (!parse_vec3_csv(line + 2, &grab_offset)) {
                printf("Invalid g format, use: g x,y,z\n");
            }
        } else if (strncmp(line, "r ", 2) == 0) {
            if (!parse_vec3_csv(line + 2, &rot_offset)) {
                printf("Invalid r format, use: r rx,ry,rz\n");
            }
        } else if (strncmp(line, "z ", 2) == 0) {
            float fv = 0.0f;
            if (parse_float_safe(line + 2, &fv)) custom_fov = fv;
            else printf("Invalid z value\n");
        } else if (strncmp(line, "m ", 2) == 0) {
            float mv = 0.0f;
            if (parse_float_safe(line + 2, &mv)) scene_scale = mv;
            else printf("Invalid m value\n");
        } else if (strncmp(line, "n ", 2) == 0) {
            int nv = 0;
            if (parse_int_safe(line + 2, &nv)) count = nv;
            else printf("Invalid n value\n");
        } else if (strncmp(line, "f ", 2) == 0) {
            int fv = 0;
            if (parse_int_safe(line + 2, &fv)) start_frame = fv;
            else printf("Invalid f value\n");
        } else if (strncmp(line, "s ", 2) == 0) {
            const char *sp = line + 2;
            while (*sp && isspace((unsigned char)*sp)) sp++;
            if (is_safe_xml_path(sp)) {
                snprintf(scene_buf, sizeof(scene_buf), "%s", sp);
            } else {
                printf("Unsafe or invalid scene path\n");
            }
        } else {
            printf("Unknown command '%s' (type 'help')\n", line);
        }
    }
    return 0;
}

static void print_help(void) {
    printf(
        "mini_blender - Blender-like CLI shortcuts, shell & graphics\n"
        "Shortcuts:\n"
        "  -h              Show this help\n"
        "  -i              Interactive shell mode\n"
        "  -p              Terminal truecolor graphics preview\n"
        "  -f <num>        Start frame (default: 0)\n"
        "  -n <num>        Frame count (default: 16)\n"
        "  -c <num>        Cycle frames (default: 32)\n"
        "  -o <dir>        Output directory (default: renders)\n"
        "  -s <file.xml>   Scene XML path\n"
        "  -g <x,y,z>      Grab/move offset\n"
        "  -r <x,y,z>      Rotate offset rx,ry,rz\n"
        "  -z <fov>        FOV / lens zoom\n"
        "  -m <scale>      Scene scale multiplier\n"
        "  --resume        Skip valid existing frames & write checkpoint\n"
        "  -q              Quiet per-frame log\n"
    );
}

int main(int argc, char **argv) {
    int start_frame = 0;
    int count = 16;
    int total_cycle = 32;
    const char *out_dir = "renders";
    const char *scene_path = NULL;
    Vec3 grab_offset = { 0.0f, 0.0f, 0.0f };
    Vec3 rot_offset = { 0.0f, 0.0f, 0.0f };
    float custom_fov = -1.0f;
    float scene_scale = 1.0f;
    int quiet = 0;
    int interactive = 0;
    int preview = 0;
    int resume_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "-i") == 0) {
            interactive = 1;
        } else if (strcmp(argv[i], "--resume") == 0) {
            resume_mode = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
#ifndef MB_NO_PREVIEW
            preview = 1;
#else
            fprintf(stderr, "Note: -p is disabled in headless build (mini_blender_pi)\n");
#endif
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            if (!parse_int_safe(argv[++i], &start_frame)) {
                fprintf(stderr, "Error: invalid -f value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            if (!parse_int_safe(argv[++i], &count)) {
                fprintf(stderr, "Error: invalid -n value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            if (!parse_int_safe(argv[++i], &total_cycle)) {
                fprintf(stderr, "Error: invalid -c value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            scene_path = argv[++i];
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            if (!parse_vec3_csv(argv[++i], &grab_offset)) {
                fprintf(stderr, "Error: invalid -g value, expected x,y,z\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            if (!parse_vec3_csv(argv[++i], &rot_offset)) {
                fprintf(stderr, "Error: invalid -r value, expected x,y,z\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-z") == 0 && i + 1 < argc) {
            if (!parse_float_safe(argv[++i], &custom_fov)) {
                fprintf(stderr, "Error: invalid -z value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            if (!parse_float_safe(argv[++i], &scene_scale)) {
                fprintf(stderr, "Error: invalid -m value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else {
            fprintf(stderr, "Error: unknown or incomplete option '%s' (use -h)\n", argv[i]);
            return 1;
        }
    }

    if (interactive) {
        return run_interactive_shell(
            start_frame, count, total_cycle, out_dir, scene_path,
            grab_offset, rot_offset, custom_fov, scene_scale, preview, resume_mode
        );
    }

    return run_batch(
        start_frame, count, total_cycle, out_dir, scene_path,
        grab_offset, rot_offset, custom_fov, scene_scale, quiet, preview, resume_mode
    );
}

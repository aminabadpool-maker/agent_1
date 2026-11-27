#include "toy_sculpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ensure_capacity(SculptScene *scene, size_t needed) {
    if (needed <= scene->capacity) {
        return 0;
    }
    size_t new_cap = scene->capacity ? scene->capacity * 2 : 64;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    SculptTri *grown = (SculptTri *)realloc(scene->tris, new_cap * sizeof(SculptTri));
    if (!grown) {
        return -1;
    }
    scene->tris = grown;
    scene->capacity = new_cap;
    return 0;
}

static int push_tri(SculptScene *scene, SculptVec3 a, SculptVec3 b, SculptVec3 c) {
    if (ensure_capacity(scene, scene->count + 1) != 0) {
        return -1;
    }
    scene->tris[scene->count].a = a;
    scene->tris[scene->count].b = b;
    scene->tris[scene->count].c = c;
    scene->tris[scene->count].active = 1;
    scene->count++;
    return 0;
}

static int emit_box_tris(SculptScene *scene, SculptBox box) {
    float x0 = box.min_x, x1 = box.max_x;
    float y0 = box.min_y, y1 = box.max_y;
    float z0 = box.min_z, z1 = box.max_z;

    SculptVec3 p000 = {x0, y0, z0}, p100 = {x1, y0, z0};
    SculptVec3 p110 = {x1, y1, z0}, p010 = {x0, y1, z0};
    SculptVec3 p001 = {x0, y0, z1}, p101 = {x1, y0, z1};
    SculptVec3 p111 = {x1, y1, z1}, p011 = {x0, y1, z1};

    if (push_tri(scene, p001, p101, p111) || push_tri(scene, p001, p111, p011)) return -1;
    if (push_tri(scene, p100, p000, p010) || push_tri(scene, p100, p010, p110)) return -1;
    if (push_tri(scene, p000, p001, p011) || push_tri(scene, p000, p011, p010)) return -1;
    if (push_tri(scene, p101, p100, p110) || push_tri(scene, p101, p110, p111)) return -1;
    if (push_tri(scene, p011, p111, p110) || push_tri(scene, p011, p110, p010)) return -1;
    if (push_tri(scene, p000, p100, p101) || push_tri(scene, p000, p101, p001)) return -1;

    return 0;
}

static int point_inside_box(SculptVec3 p, SculptBox box) {
    return (p.x >= box.min_x && p.x <= box.max_x) &&
           (p.y >= box.min_y && p.y <= box.max_y) &&
           (p.z >= box.min_z && p.z <= box.max_z);
}

static int tri_inside_box(SculptTri t, SculptBox box) {
    SculptVec3 cen = {
        (t.a.x + t.b.x + t.c.x) / 3.0f,
        (t.a.y + t.b.y + t.c.y) / 3.0f,
        (t.a.z + t.b.z + t.c.z) / 3.0f
    };
    if (point_inside_box(cen, box)) {
        return 1;
    }
    int inside_verts = point_inside_box(t.a, box) +
                       point_inside_box(t.b, box) +
                       point_inside_box(t.c, box);
    return inside_verts >= 2;
}

static SculptVec3 relax_vertex(SculptVec3 v, SculptVec3 center, float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 0.5f) amount = 0.5f;
    SculptVec3 out;
    out.x = v.x + (center.x - v.x) * amount;
    out.y = v.y + (center.y - v.y) * amount;
    out.z = v.z + (center.z - v.z) * amount;
    return out;
}

int sculpt_scene_init(SculptScene *scene, size_t initial_cap) {
    if (!scene) return -1;
    memset(scene, 0, sizeof(*scene));
    if (initial_cap > 0) {
        scene->tris = (SculptTri *)malloc(initial_cap * sizeof(SculptTri));
        if (!scene->tris) return -1;
        scene->capacity = initial_cap;
    }
    scene->cube_mode = SCULPT_MODE_ADD;
    scene->smooth_strength = 0.12f;
    SculptBox starter = {-0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f};
    scene->last_box = starter;
    scene->cube = starter;
    return 0;
}

void sculpt_scene_free(SculptScene *scene) {
    if (!scene) return;
    free(scene->tris);
    free(scene->prev_tris);
    memset(scene, 0, sizeof(*scene));
}

int sculpt_checkpoint(SculptScene *scene) {
    if (!scene) return -1;
    if (scene->count > scene->prev_capacity) {
        SculptTri *buf = (SculptTri *)realloc(scene->prev_tris, scene->count * sizeof(SculptTri));
        if (!buf) return -1;
        scene->prev_tris = buf;
        scene->prev_capacity = scene->count;
    }
    if (scene->count > 0) {
        memcpy(scene->prev_tris, scene->tris, scene->count * sizeof(SculptTri));
    }
    scene->prev_count = scene->count;
    scene->has_prev = 1;
    return 0;
}

int sculpt_undo(SculptScene *scene) {
    if (!scene || !scene->has_prev) return -1;
    if (ensure_capacity(scene, scene->prev_count) != 0) return -1;
    if (scene->prev_count > 0) {
        memcpy(scene->tris, scene->prev_tris, scene->prev_count * sizeof(SculptTri));
    }
    scene->count = scene->prev_count;
    return 0;
}

int sculpt_clear(SculptScene *scene) {
    if (!scene) return -1;
    if (sculpt_checkpoint(scene) != 0) return -1;
    scene->count = 0;
    return 0;
}

int sculpt_reset(SculptScene *scene) {
    if (!scene) return -1;
    if (sculpt_clear(scene) != 0) return -1;
    scene->cube_mode = SCULPT_MODE_ADD;
    SculptBox starter = {-0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f};
    scene->last_box = starter;
    scene->cube = starter;
    return emit_box_tris(scene, starter);
}

void sculpt_set_cube_mode(SculptScene *scene, SculptCubeMode mode) {
    if (!scene) return;
    scene->cube_mode = mode;
}

void sculpt_set_cube_box(SculptScene *scene, SculptBox box) {
    if (!scene) return;
    scene->cube = box;
}

int sculpt_add_box(SculptScene *scene, SculptBox box) {
    if (!scene) return -1;
    if (sculpt_checkpoint(scene) != 0) return -1;
    scene->last_box = box;
    return emit_box_tris(scene, box);
}

int sculpt_clone_box(SculptScene *scene, SculptBox src, float dx, float dy, float dz) {
    if (!scene) return -1;
    SculptBox moved = {
        src.min_x + dx, src.max_x + dx,
        src.min_y + dy, src.max_y + dy,
        src.min_z + dz, src.max_z + dz
    };
    return sculpt_add_box(scene, moved);
}

size_t sculpt_apply_cube(SculptScene *scene) {
    if (!scene) return 0;
    if (sculpt_checkpoint(scene) != 0) return 0;

    if (scene->cube_mode == SCULPT_MODE_ADD) {
        if (emit_box_tris(scene, scene->cube) == 0) {
            scene->last_box = scene->cube;
            return 12;
        }
        return 0;
    }

    size_t erased = 0;
    for (size_t i = 0; i < scene->count; ++i) {
        if (scene->tris[i].active && tri_inside_box(scene->tris[i], scene->cube)) {
            scene->tris[i].active = 0;
            erased++;
        }
    }
    return erased;
}

void sculpt_smooth_step(SculptScene *scene, float amount) {
    if (!scene || scene->count == 0) return;
    if (sculpt_checkpoint(scene) != 0) return;

    SculptVec3 sum = {0.0f, 0.0f, 0.0f};
    size_t active_tris = 0;
    for (size_t i = 0; i < scene->count; ++i) {
        if (!scene->tris[i].active) continue;
        sum.x += (scene->tris[i].a.x + scene->tris[i].b.x + scene->tris[i].c.x) / 3.0f;
        sum.y += (scene->tris[i].a.y + scene->tris[i].b.y + scene->tris[i].c.y) / 3.0f;
        sum.z += (scene->tris[i].a.z + scene->tris[i].b.z + scene->tris[i].c.z) / 3.0f;
        active_tris++;
    }
    if (active_tris == 0) return;

    SculptVec3 center = {
        sum.x / (float)active_tris,
        sum.y / (float)active_tris,
        sum.z / (float)active_tris
    };

    for (size_t i = 0; i < scene->count; ++i) {
        if (!scene->tris[i].active) continue;
        scene->tris[i].a = relax_vertex(scene->tris[i].a, center, amount);
        scene->tris[i].b = relax_vertex(scene->tris[i].b, center, amount);
        scene->tris[i].c = relax_vertex(scene->tris[i].c, center, amount);
    }
}

int sculpt_handle_shortcut(SculptScene *scene, SculptShortcut key) {
    if (!scene) return -1;
    switch (key) {
        case SCULPT_KEY_CLONE:
            return sculpt_clone_box(scene, scene->last_box, 0.65f, 0.0f, 0.0f);
        case SCULPT_KEY_ATTACH_ADD:
            sculpt_set_cube_mode(scene, SCULPT_MODE_ADD);
            return sculpt_add_box(scene, scene->cube);
        case SCULPT_KEY_ERASE_CUBE:
            sculpt_set_cube_mode(scene, SCULPT_MODE_ERASE);
            return (sculpt_apply_cube(scene) > 0 || scene->count == 0) ? 0 : 0;
        case SCULPT_KEY_SMOOTH:
            sculpt_smooth_step(scene, scene->smooth_strength);
            return 0;
        case SCULPT_KEY_UNDO:
            return sculpt_undo(scene);
        case SCULPT_KEY_SAFE_RESET:
            return sculpt_reset(scene);
        case SCULPT_KEY_NONE:
        default:
            return 0;
    }
}

void sculpt_run_tutorial_35s(SculptScene *scene) {
    if (!scene) return;

    SculptBox torso = {-1.0f, 1.0f, -0.4f, 0.4f, -0.5f, 0.5f};
    SculptBox neck  = { 0.6f, 1.2f,  0.2f, 1.0f, -0.3f, 0.3f};
    sculpt_add_box(scene, torso);
    sculpt_add_box(scene, neck);
    printf("[0-10s] Step 1: Simple blocks placed (doesn't have to be perfect yet!).\n");

    SculptBox head_src = {0.9f, 1.6f, 0.8f, 1.3f, -0.25f, 0.25f};
    sculpt_clone_box(scene, head_src, 0.0f, 0.0f, 0.0f);
    printf("[10-20s] Step 2: Cloned & attached a new block onto the form.\n");

    sculpt_smooth_step(scene, scene->smooth_strength);
    printf("[20-30s] Step 3: Applied gentle smooth step.\n");

    SculptBox eraser_tip = {1.35f, 1.65f, 1.05f, 1.35f, -0.3f, 0.3f};
    sculpt_set_cube_mode(scene, SCULPT_MODE_ERASE);
    sculpt_set_cube_box(scene, eraser_tip);
    size_t carved = sculpt_apply_cube(scene);
    printf("[30-40s] Step 4: Eraser cube trimmed %zu triangles (safe reset/undo ready).\n", carved);
}

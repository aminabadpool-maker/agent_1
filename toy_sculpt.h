#ifndef TOY_SCULPT_H
#define TOY_SCULPT_H

#include <stddef.h>

typedef struct {
    float x, y, z;
} SculptVec3;

typedef struct {
    SculptVec3 a, b, c;
    int active; /* 1 = visible, 0 = erased by magic cube */
} SculptTri;

typedef struct {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
} SculptBox;

typedef enum {
    SCULPT_MODE_ADD = 0,
    SCULPT_MODE_ERASE = 1
} SculptCubeMode;

typedef struct {
    SculptTri *tris;
    size_t count;
    size_t capacity;

    /* Snapshot for safe undo (no panic when messing up) */
    SculptTri *prev_tris;
    size_t prev_count;
    size_t prev_capacity;
    int has_prev;

    /* Magic cube tool state */
    SculptCubeMode cube_mode;
    SculptBox cube;
    float smooth_strength; /* e.g. 0.0f .. 0.35f per step */
} SculptScene;

/* Lifecycle & safety */
int sculpt_scene_init(SculptScene *scene, size_t initial_cap);
void sculpt_scene_free(SculptScene *scene);
int sculpt_checkpoint(SculptScene *scene);
int sculpt_undo(SculptScene *scene);

/* Discrete actions (usable by human shortcuts, UI, or AI/ML agents) */
void sculpt_set_cube_mode(SculptScene *scene, SculptCubeMode mode);
void sculpt_set_cube_box(SculptScene *scene, SculptBox box);
int sculpt_add_box(SculptScene *scene, SculptBox box);
int sculpt_clone_box(SculptScene *scene, SculptBox src, float dx, float dy, float dz);
size_t sculpt_apply_cube(SculptScene *scene);
void sculpt_smooth_step(SculptScene *scene, float amount);

/* Friendly ~35s 4-step starter routine */
void sculpt_run_tutorial_35s(SculptScene *scene);

#endif

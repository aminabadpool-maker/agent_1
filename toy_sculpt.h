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

typedef enum {
    SCULPT_KEY_NONE = 0,
    SCULPT_KEY_CLONE,      /* e.g. Shift+C */
    SCULPT_KEY_ATTACH_ADD, /* e.g. Shift+D / add block */
    SCULPT_KEY_ERASE_CUBE, /* e.g. Shift+R / magic cube erase */
    SCULPT_KEY_SMOOTH,     /* e.g. Shift+S / gentle smooth */
    SCULPT_KEY_UNDO,       /* e.g. Ctrl+Z / step back */
    SCULPT_KEY_SAFE_RESET  /* e.g. Shift+R+W / clear scene safely */
} SculptShortcut;

typedef struct {
    SculptTri *tris;
    size_t count;
    size_t capacity;

    /* Snapshot for safe undo (even after clear/reset) */
    SculptTri *prev_tris;
    size_t prev_count;
    size_t prev_capacity;
    int has_prev;

    /* Magic cube & default box state */
    SculptCubeMode cube_mode;
    SculptBox cube;
    SculptBox last_box;
    float smooth_strength; /* e.g. 0.0f .. 0.35f per step */
} SculptScene;

/* Lifecycle & safety */
int sculpt_scene_init(SculptScene *scene, size_t initial_cap);
void sculpt_scene_free(SculptScene *scene);
int sculpt_checkpoint(SculptScene *scene);
int sculpt_undo(SculptScene *scene);
int sculpt_clear(SculptScene *scene);
int sculpt_reset(SculptScene *scene);

/* Discrete actions (usable by shortcuts, UI, or AI/ML agents) */
void sculpt_set_cube_mode(SculptScene *scene, SculptCubeMode mode);
void sculpt_set_cube_box(SculptScene *scene, SculptBox box);
int sculpt_add_box(SculptScene *scene, SculptBox box);
int sculpt_clone_box(SculptScene *scene, SculptBox src, float dx, float dy, float dz);
size_t sculpt_apply_cube(SculptScene *scene);
void sculpt_smooth_step(SculptScene *scene, float amount);

/* Simple shortcut dispatcher for humans & agents */
int sculpt_handle_shortcut(SculptScene *scene, SculptShortcut key);

/* Friendly ~35s 4-step starter routine */
void sculpt_run_tutorial_35s(SculptScene *scene);

#endif

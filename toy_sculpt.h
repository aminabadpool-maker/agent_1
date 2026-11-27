#ifndef TOY_SCULPT_H
#define TOY_SCULPT_H

#include <stddef.h>

typedef struct {
    float x, y, z;
} SculptVec3;

typedef struct {
    SculptVec3 a, b, c;
    int active; /* 1 = visible on screen, 0 = carved by magic cube */
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
    SCULPT_KEY_CLONE,      /* Copy & place nearby block */
    SCULPT_KEY_ATTACH_ADD, /* Attach/add block */
    SCULPT_KEY_ERASE_CUBE, /* Magic cube eraser mode */
    SCULPT_KEY_SMOOTH,     /* Gentle smooth step */
    SCULPT_KEY_UNDO,       /* Step back safely */
    SCULPT_KEY_SAFE_RESET  /* Friendly safe reset (undoable) */
} SculptShortcut;

typedef struct {
    SculptTri *tris;
    size_t count;
    size_t capacity;

    /* Snapshot for stress-free undo */
    SculptTri *prev_tris;
    size_t prev_count;
    size_t prev_capacity;
    int has_prev;

    /* Magic cube & default box state */
    SculptCubeMode cube_mode;
    SculptBox cube;
    SculptBox last_box;
    float smooth_strength; /* Safe range clamped internally */
} SculptScene;

/* Lifecycle & safety */
int sculpt_scene_init(SculptScene *scene, size_t initial_cap);
void sculpt_scene_free(SculptScene *scene);
int sculpt_checkpoint(SculptScene *scene);
int sculpt_undo(SculptScene *scene);
int sculpt_clear(SculptScene *scene);
int sculpt_reset(SculptScene *scene);
size_t sculpt_active_count(const SculptScene *scene);

/* Discrete actions (usable by shortcuts, UI, or AI/ML agents) */
void sculpt_set_cube_mode(SculptScene *scene, SculptCubeMode mode);
void sculpt_set_cube_box(SculptScene *scene, SculptBox box);
int sculpt_add_box(SculptScene *scene, SculptBox box);
int sculpt_clone_box(SculptScene *scene, SculptBox src, float dx, float dy, float dz);
size_t sculpt_apply_cube(SculptScene *scene);
void sculpt_smooth_step(SculptScene *scene, float amount);

/* Friendly shortcut & char dispatchers */
SculptShortcut sculpt_map_char(char ch);
int sculpt_handle_shortcut(SculptScene *scene, SculptShortcut key);
int sculpt_handle_char(SculptScene *scene, char ch);

/* Friendly ~35s 4-step starter routine */
void sculpt_run_tutorial_35s(SculptScene *scene);

#endif

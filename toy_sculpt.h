#ifndef TOY_SCULPT_H
#define TOY_SCULPT_H

#include <stddef.h>

typedef struct {
    float x, y, z;
} SculptVec3;

typedef struct {
    SculptVec3 a, b, c;
    int active;
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
    SCULPT_KEY_CLONE,
    SCULPT_KEY_ATTACH_ADD,
    SCULPT_KEY_ERASE_CUBE,
    SCULPT_KEY_SMOOTH,
    SCULPT_KEY_UNDO,
    SCULPT_KEY_SAFE_RESET,
    SCULPT_KEY_STARTER,
    SCULPT_KEY_STATUS
} SculptShortcut;

typedef struct {
    SculptTri *tris;
    size_t count;
    size_t capacity;

    SculptTri *prev_tris;
    size_t prev_count;
    size_t prev_capacity;
    int has_prev;

    SculptCubeMode cube_mode;
    SculptBox cube;
    SculptBox last_box;
    float smooth_strength;
} SculptScene;

int sculpt_scene_init(SculptScene *scene, size_t initial_cap);
void sculpt_scene_free(SculptScene *scene);
int sculpt_checkpoint(SculptScene *scene);
int sculpt_undo(SculptScene *scene);
int sculpt_clear(SculptScene *scene);
int sculpt_reset(SculptScene *scene);
int sculpt_spawn_starter(SculptScene *scene);
size_t sculpt_active_count(const SculptScene *scene);
void sculpt_print_status(const SculptScene *scene);

void sculpt_set_cube_mode(SculptScene *scene, SculptCubeMode mode);
void sculpt_set_cube_box(SculptScene *scene, SculptBox box);
int sculpt_add_box(SculptScene *scene, SculptBox box);
int sculpt_clone_box(SculptScene *scene, SculptBox src, float dx, float dy, float dz);
size_t sculpt_apply_cube(SculptScene *scene);
void sculpt_smooth_step(SculptScene *scene, float amount);

SculptShortcut sculpt_map_char(char ch);
int sculpt_handle_shortcut(SculptScene *scene, SculptShortcut key);
int sculpt_handle_char(SculptScene *scene, char ch);

void sculpt_run_tutorial_35s(SculptScene *scene);

#endif

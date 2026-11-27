#include "toy_sculpt.h"
#include <stdio.h>

int main(void) {
    SculptScene scene;
    if (sculpt_scene_init(&scene, 128) != 0) {
        fprintf(stderr, "Failed to init SculptScene\n");
        return 1;
    }

    sculpt_run_tutorial_35s(&scene);
    printf("After 35s tutorial: %zu tris\n", scene.count);

    sculpt_handle_shortcut(&scene, SCULPT_KEY_CLONE);
    printf("After clone shortcut: %zu tris\n", scene.count);

    sculpt_handle_shortcut(&scene, SCULPT_KEY_SAFE_RESET);
    printf("After safe reset: %zu tris\n", scene.count);

    sculpt_undo(&scene);
    printf("After undo of safe reset: %zu tris\n", scene.count);

    sculpt_scene_free(&scene);
    return 0;
}

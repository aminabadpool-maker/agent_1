#include "toy_sculpt.h"
#include <stdio.h>

int main(void) {
    SculptScene scene;
    if (sculpt_scene_init(&scene, 128) != 0) {
        fprintf(stderr, "Failed to init SculptScene\n");
        return 1;
    }

    sculpt_run_tutorial_35s(&scene);
    printf("Total triangles: %zu\n", scene.count);

    sculpt_undo(&scene);
    printf("After undo, total triangles: %zu\n", scene.count);

    sculpt_scene_free(&scene);
    return 0;
}

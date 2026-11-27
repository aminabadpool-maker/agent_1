#include "toy_sculpt.h"
#include <stdio.h>

int main(void) {
    SculptScene scene;
    if (sculpt_scene_init(&scene, 128) != 0) {
        fprintf(stderr, "Failed to init SculptScene\n");
        return 1;
    }

    sculpt_run_tutorial_35s(&scene);
    printf("After 35s tutorial: active=%zu / total=%zu\n",
           sculpt_active_count(&scene), scene.count);

    sculpt_handle_char(&scene, 'c');
    printf("After clone ('c'): active=%zu / total=%zu\n",
           sculpt_active_count(&scene), scene.count);

    sculpt_handle_char(&scene, 'x');
    printf("After safe reset ('x'): active=%zu / total=%zu\n",
           sculpt_active_count(&scene), scene.count);

    sculpt_handle_char(&scene, 'u');
    printf("After undo ('u'): active=%zu / total=%zu\n",
           sculpt_active_count(&scene), scene.count);

    sculpt_scene_free(&scene);
    return 0;
}

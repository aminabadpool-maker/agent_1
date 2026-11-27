#include "toy_sculpt.h"
#include <stdio.h>

int main(void) {
    SculptScene scene;
    if (sculpt_scene_init(&scene, 128) != 0) {
        fprintf(stderr, "Failed to init SculptScene\n");
        return 1;
    }

    sculpt_handle_char(&scene, 'b');
    sculpt_handle_char(&scene, '?');

    sculpt_handle_char(&scene, 'm');
    sculpt_handle_char(&scene, '?');

    sculpt_run_tutorial_35s(&scene);
    sculpt_handle_char(&scene, '?');

    sculpt_handle_char(&scene, 'o');
    sculpt_handle_char(&scene, '?');
    sculpt_handle_char(&scene, 'p');
    sculpt_handle_char(&scene, '?');
    sculpt_handle_char(&scene, 'k');
    sculpt_handle_char(&scene, '?');

    sculpt_handle_char(&scene, 'l');
    sculpt_handle_char(&scene, '?');

    sculpt_scene_free(&scene);
    return 0;
}

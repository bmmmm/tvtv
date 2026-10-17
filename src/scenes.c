// SPDX-License-Identifier: GPL-3.0-or-later
// The scene table: the default loop in play order, plus scenes selectable by name only.
// A new scene is declared here and appended to one of the two lists.
#include "scenes.h"

#include <stdio.h>
#include <string.h>

extern const Scene scene_boot, scene_bounce, scene_explode, scene_plasma, scene_stars, scene_tunnel,
    scene_matrix, scene_juggle, scene_fire, scene_glitch, scene_outrun, scene_flowers, scene_rainbow,
    scene_kaleido, scene_hippie, scene_crt, scene_bernd, scene_explain, scene_stack, scene_testcard;

const Scene *const scene_loop[] = {
    &scene_boot, &scene_flowers, &scene_bounce, &scene_rainbow, &scene_explode, &scene_kaleido,
    &scene_plasma, &scene_stars, &scene_hippie, &scene_tunnel, &scene_matrix, &scene_juggle,
    &scene_fire, &scene_glitch, &scene_outrun, &scene_crt, &scene_bernd,
};
const int scene_nloop = (int)(sizeof scene_loop / sizeof scene_loop[0]);

const Scene *const scene_extra[] = {&scene_explain, &scene_stack, &scene_testcard};
const int scene_nextra = (int)(sizeof scene_extra / sizeof scene_extra[0]);

const Scene *scene_find(const char *name) {
    for (int k = 0; k < scene_nloop; k++)
        if (!strcmp(name, scene_loop[k]->name)) return scene_loop[k];
    for (int k = 0; k < scene_nextra; k++)
        if (!strcmp(name, scene_extra[k]->name)) return scene_extra[k];
    return NULL;
}

void scene_list(void) {
    for (int k = 0; k < scene_nloop; k++) printf("%-8s %4.0fs\n", scene_loop[k]->name, scene_loop[k]->dur);
    for (int k = 0; k < scene_nextra; k++) printf("%-8s slides (extra)\n", scene_extra[k]->name);
}

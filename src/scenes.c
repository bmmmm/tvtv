// SPDX-License-Identifier: GPL-3.0-or-later
// The scene table: the default loop in play order, plus scenes selectable by name or number only.
// A new built-in scene is declared here and appended to one of the two lists; uploaded visuals
// arrive through build/visuals.c, which tools/gen-visuals.sh generates and every build links.
#include "scenes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

int scene_channel(const Scene *sc) {
    if (sc == &scene_black) return 0;
    for (int k = 0; k < scene_nloop; k++)
        if (sc == scene_loop[k]) return 1 + k;
    for (int k = 0; k < scene_nextra; k++)
        if (sc == scene_extra[k]) return CH_EXTRA + k;
    for (int k = 0; k < scene_nvisuals; k++)
        if (sc == scene_visuals[k]) return CH_VISUAL + k;
    return -1;
}

const Scene *scene_by_channel(int ch) {
    if (ch == 0) return &scene_black;
    if (ch >= 1 && ch <= scene_nloop) return scene_loop[ch - 1];
    if (ch >= CH_EXTRA && ch < CH_EXTRA + scene_nextra) return scene_extra[ch - CH_EXTRA];
    if (ch >= CH_VISUAL && ch < CH_VISUAL + scene_nvisuals) return scene_visuals[ch - CH_VISUAL];
    return NULL;
}

const Scene *scene_find(const char *name) {
    for (int ch = 0; ch <= CH_MAX; ch++) {
        const Scene *sc = scene_by_channel(ch);
        if (sc && !strcmp(name, sc->name)) return sc;
    }
    return NULL;
}

const Scene *scene_parse(const char *s) {
    if (!s || !*s) return NULL;
    if (isdigit((unsigned char)s[0])) {
        char *end;
        long ch = strtol(s, &end, 10);
        return (*end == 0 && ch >= 0 && ch <= CH_MAX) ? scene_by_channel((int)ch) : NULL;
    }
    return scene_find(s);
}

const char *scene_kind(const Scene *sc) {
    int ch = scene_channel(sc);
    if (ch == 0) return "standby";
    if (ch >= CH_VISUAL) return "visual";
    if (ch >= CH_EXTRA) return "extra";
    return ch > 0 ? "loop" : "?";
}

size_t scene_list_str(char *out, size_t n) {
    size_t len = 0;
    for (int ch = 0; ch <= CH_MAX && len < n; ch++) {
        const Scene *sc = scene_by_channel(ch);
        if (!sc) continue;
        if (sc->dur > 0 && !sc->length)
            len += (size_t)snprintf(out + len, n - len, "%2d %-8s %4.0fs %s\n", ch, sc->name, sc->dur, scene_kind(sc));
        else // slides: the length is known after init
            len += (size_t)snprintf(out + len, n - len, "%2d %-8s     - %s\n", ch, sc->name, scene_kind(sc));
    }
    return len < n ? len : n - 1;
}

void scene_list(void) {
    char out[4096];
    scene_list_str(out, sizeof out);
    fputs(out, stdout);
}

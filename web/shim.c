// SPDX-License-Identifier: GPL-3.0-or-later
// Browser build: the scenes compiled to WebAssembly, the framebuffer replaced by an RGBA
// buffer that web/player.js copies onto a canvas. Sequencing and fades mirror main.c.
#include "scenes.h"

#include <emscripten.h>

#define FADE 0.4f

static uint32_t rgba[W * H];
static const Scene *cur;
static float cur_t, cur_dur;
static int cur_idx = -1; // 0..nloop-1 = loop scenes, then the extras
static int hold;         // 1 = restart the current scene instead of moving on

static const Scene *scene_at(int i) {
    if (i >= 0 && i < scene_nloop) return scene_loop[i];
    i -= scene_nloop;
    if (i >= 0 && i < scene_nextra) return scene_extra[i];
    return NULL;
}

// Fonts and slides are embedded at /fonts and /slides; without asset_init() the asset()
// paths stay relative, and the embedded filesystem's working directory is /.
EMSCRIPTEN_KEEPALIVE int web_init(void) {
    return load_font(&fbig, "fonts/big.psf") || load_font(&fsml, "fonts/small.psf") ? -1 : 0;
}
EMSCRIPTEN_KEEPALIVE int web_w(void) { return W; }
EMSCRIPTEN_KEEPALIVE int web_h(void) { return H; }
EMSCRIPTEN_KEEPALIVE int web_scene_count(void) { return scene_nloop + scene_nextra; }
EMSCRIPTEN_KEEPALIVE int web_loop_count(void) { return scene_nloop; }
EMSCRIPTEN_KEEPALIVE const char *web_scene_name(int i) {
    const Scene *s = scene_at(i);
    return s ? s->name : "";
}
EMSCRIPTEN_KEEPALIVE float web_scene_dur(int i) {
    const Scene *s = scene_at(i);
    return s ? s->dur : 0;
}
EMSCRIPTEN_KEEPALIVE void web_start(int i, int stay) {
    const Scene *s = scene_at(i);
    if (!s) return;
    cur = s;
    cur_idx = i;
    cur_t = 0;
    hold = stay;
    if (s->init) s->init();
    cur_dur = s->length ? s->length() : s->dur;
}
EMSCRIPTEN_KEEPALIVE int web_current(void) { return cur_idx; }
EMSCRIPTEN_KEEPALIVE float web_time(void) { return cur_t; }
EMSCRIPTEN_KEEPALIVE float web_length(void) { return cur_dur; }

// Advance the show by dt seconds and render one frame. A finished scene is followed by the
// next one of the loop, or restarted when held; extras (the slide decks) restart by nature.
EMSCRIPTEN_KEEPALIVE uint32_t *web_step(float dt) {
    if (!cur) web_start(0, 0);
    if (dt > 0.05f) dt = 0.05f;
    cur_t += dt;
    if (cur_t >= cur_dur) {
        int next = hold || cur_idx >= scene_nloop ? cur_idx : (cur_idx + 1) % scene_nloop;
        web_start(next, hold);
    }
    cur->frame(cur_t, dt);
    float fade = fminf(1.0f, fminf(cur_t / FADE, (cur_dur - cur_t) / FADE));
    int k = fade < 1.0f ? (int)(fade * 256) : 256;
    for (int i = 0; i < W * H; i++) {
        uint16_t c = k < 256 ? scale16(buf[i], k) : buf[i];
        uint32_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
        rgba[i] = 0xFF000000u | (b << 16) | (g << 8) | r; // RGBA byte order in memory
    }
    return rgba;
}

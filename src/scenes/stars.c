// SPDX-License-Identifier: GPL-3.0-or-later
// Accelerating warp starfield with a slow roll; the URL zooms in from deep space.
#include "core.h"

#define NS 400

static float sx[NS], sy[NS], sz[NS];
static int ox[NS], oy[NS];

static void reset(int i, int far) {
    sx[i] = frand() * 2 - 1;
    sy[i] = frand() * 2 - 1;
    sz[i] = far ? 1.0f : 0.05f + frand() * 0.95f;
    ox[i] = -1;
}

static void init(void) {
    for (int i = 0; i < NS; i++) reset(i, 0);
}

static void frame(float t, float dt) {
    clear(rgb(0, 0, 8));
    float speed = 0.15f + fminf(t * 0.18f, 1.2f);
    float ang = t * 0.25f, ca = cosf(ang), sa = sinf(ang);
    for (int i = 0; i < NS; i++) {
        sz[i] -= speed * dt;
        if (sz[i] < 0.02f) { reset(i, 1); continue; }
        float x = sx[i] * ca - sy[i] * sa, y = sx[i] * sa + sy[i] * ca;
        int X = (int)(W / 2 + x / sz[i] * 120), Y = (int)(H / 2 + y / sz[i] * 120);
        if (X < 0 || X >= W || Y < 0 || Y >= H) { reset(i, 1); continue; }
        int br = (int)(255 * (1 - sz[i]));
        uint16_t c = rgb(br * 3 / 4, br * 7 / 8, br);
        if (ox[i] >= 0) line(ox[i], oy[i], X, Y, c);
        else px(X, Y, c);
        ox[i] = X;
        oy[i] = Y;
    }
    if (t > 1.5f) {
        float k = 1.0f - expf(-(t - 1.5f) * 0.9f);
        float s = 0.05f + 1.35f * k;
        text_zoom(&fbig, "bitcircus101.de", W / 2 + 2, H / 2 + 2, s, rgb(40, 20, 0));
        text_zoom(&fbig, "bitcircus101.de", W / 2, H / 2, s, rgb(255, 200, 60));
    }
}

const Scene scene_stars = {"stars", 9, init, frame};

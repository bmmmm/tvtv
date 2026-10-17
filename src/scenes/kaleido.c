// SPDX-License-Identifier: GPL-3.0-or-later
// Eight-fold rainbow kaleidoscope with a spinning flower in the middle.
#include "core.h"

#define SEG 8

static uint8_t ku[W * H], kv[W * H];
static int8_t st[256];
static uint16_t pal[256];
static int ready;

static void init(void) {
    if (ready) return;
    ready = 1;
    float wedge = 2 * PI / SEG;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = x - W / 2.0f + 0.5f, dy = y - H / 2.0f + 0.5f;
            float a = atan2f(dy, dx) + PI, r = sqrtf(dx * dx + dy * dy);
            a = fmodf(a, wedge);
            if (a > wedge / 2) a = wedge - a;
            ku[y * W + x] = (uint8_t)((int)(r * cosf(a)) & 255);
            kv[y * W + x] = (uint8_t)((int)(r * sinf(a) * 2) & 255);
        }
    for (int i = 0; i < 256; i++) {
        st[i] = (int8_t)(127 * sinf(i * 2 * PI / 256));
        pal[i] = hsv16(i / 256.0f, 0.9f, 0.55f + 0.45f * fabsf(sinf(i * PI * 4 / 256)));
    }
}

static void frame(float t, float dt) {
    (void)dt;
    int a1 = (int)(t * 60), a2 = (int)(t * -45), a3 = (int)(t * 30), a4 = (int)(t * 80), rot = (int)(t * 50);
    int zoom = 256 + (int)(sinf(t * 0.7f) * 90);
    for (int i = 0; i < W * H; i++) {
        int u = (ku[i] * zoom) >> 8, v = (kv[i] * zoom) >> 8;
        int s = st[(u + a1) & 255] + st[(v + a2) & 255] + st[(u + v + a3) & 255] + st[(st[(u * 2 + a4) & 255] + v) & 255];
        buf[i] = pal[((s >> 2) + rot) & 255];
    }
    int cx = W / 2, cy = H / 2;
    for (int k = 0; k < 8; k++) {
        float a = k * 2 * PI / 8 - t * 1.5f;
        circle(cx + (int)(cosf(a) * 20), cy + (int)(sinf(a) * 20), 12, hsv16(k / 8.0f + t * 0.3f, 0.6f, 1.0f));
    }
    circle(cx, cy, 13, rgb(255, 230, 60));
    circle(cx, cy, 6, rgb(255, 130, 20));
    text_shadow(&fbig, "bitcircus101.de", (W - text_w(&fbig, "bitcircus101.de", 1)) / 2, 248, 1, rgb(255, 255, 255));
}

const Scene scene_kaleido = {"kaleido", 10, init, frame};

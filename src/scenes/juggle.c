// SPDX-License-Identifier: GPL-3.0-or-later
// A stick-figure clown juggles the letters B-C-1-0-1 in a 5-ball cascade under the big top.
#include "core.h"

#include <string.h>

#define BEAT 0.21f
#define HY 196
#define NB 5

static const char balls[NB] = {'B', 'C', '1', '0', '1'};
static const float hin[2] = {168, 192}, hout[2] = {136, 224};

static void hand_pos(int h, float s, float *x, float *y) {
    float k = floorf(s);
    if (((int)k & 1) != h) k -= 1;
    float since = s - k;
    if (since < 1) {
        *x = hin[h] + (hout[h] - hin[h]) * since;
        *y = HY - 6 * sinf(PI * since);
    } else {
        float w = since - 1;
        *x = hout[h] + (hin[h] - hout[h]) * w;
        *y = HY + 10 * sinf(PI * w);
    }
}

static void ball_pos(int i, float s, float *x, float *y) {
    float s2 = s - i, m = floorf(s2 / 5), f = s2 - 5 * m;
    int k = i + 5 * (int)m, from = k & 1, to = 1 - from;
    if (f < 4) {
        float u = f / 4;
        *x = hin[from] + (hout[to] - hin[from]) * u;
        *y = HY - 4 * 115 * u * (1 - u);
    } else {
        float w = f - 4;
        *x = hout[to] + (hin[to] - hout[to]) * w;
        *y = HY + 10 * sinf(PI * w);
    }
}

static void thick(int x0, int y0, int x1, int y1, uint16_t c) {
    line(x0, y0, x1, y1, c);
    line(x0 + 1, y0, x1 + 1, y1, c);
}

static void frame(float t, float dt) {
    (void)dt;
    // big-top curtain stripes
    for (int y = 0; y < 236; y++) {
        int off = (int)(sinf(y * 0.02f + t) * 6) + 240;
        uint16_t *row = buf + y * W;
        uint16_t a = rgb(120, 15, 25), b = rgb(170, 140, 100);
        for (int x = 0; x < W; x++) row[x] = (((x + off) / 24) & 1) ? a : b;
    }
    rect(0, 236, W, H - 236, rgb(70, 40, 20));
    for (int dy = -14; dy <= 14; dy++) {
        int dx = (int)(70 * sqrtf(1 - (dy * dy) / 196.0f));
        for (int x = 180 - dx; x <= 180 + dx; x++) px(x, 248 + dy, rgb(140, 105, 60));
    }
    for (int i = 0; i < 80; i++) {
        float a = i * 2 * PI / 80;
        px(180 + (int)(cosf(a) * 170), 252 + (int)(sinf(a) * 24), (i & 1) ? rgb(255, 60, 60) : rgb(255, 255, 255));
    }

    // clown
    uint16_t body = rgb(250, 250, 250), skin = rgb(250, 210, 170);
    thick(180, 160, 180, 206, body);
    thick(180, 206, 166, 236, body);
    thick(180, 206, 194, 236, body);
    float s = t / BEAT;
    for (int h = 0; h < 2; h++) {
        float hx, hy;
        hand_pos(h, s, &hx, &hy);
        int shx = h ? 186 : 174, ex = (shx + (int)hx) / 2 + (h ? 6 : -6), ey = (168 + (int)hy) / 2 + 10;
        thick(shx, 168, ex, ey, body);
        thick(ex, ey, (int)hx, (int)hy, body);
        circle((int)hx, (int)hy, 3, skin);
    }
    circle(180, 150, 9, skin);
    circle(180, 152, 2, rgb(255, 0, 0));
    rect(172, 128, 17, 14, rgb(10, 10, 10));
    rect(168, 140, 25, 3, rgb(10, 10, 10));
    rect(172, 136, 17, 3, rgb(200, 30, 30));

    // juggled letters
    for (int i = 0; i < NB; i++) {
        float bx, by;
        ball_pos(i, s, &bx, &by);
        int x = (int)bx, y = (int)by - 12;
        circle(x, y, 10, hsv16(i / (float)NB + 0.05f, 0.85f, 1.0f));
        px(x - 4, y - 5, rgb(255, 255, 255));
        px(x - 3, y - 5, rgb(255, 255, 255));
        glyph(&fsml, (unsigned char)balls[i], x - 4, y - 8, 1, rgb(10, 10, 30));
    }

    // marquee title with chasing lights
    const char *title = "BITCIRCUS101";
    int n = strlen(title), x0 = (W - n * fbig.w) / 2;
    for (int i = 0; i < n; i++) {
        int y = 6 + (int)(sinf(t * 4 + i * 0.5f) * 3);
        uint16_t c = ((i + (int)(t * 6)) & 1) ? rgb(255, 210, 60) : rgb(255, 80, 60);
        glyph(&fbig, (unsigned char)title[i], x0 + i * fbig.w + 2, y + 2, 1, rgb(40, 0, 0));
        glyph(&fbig, (unsigned char)title[i], x0 + i * fbig.w, y, 1, c);
    }
    for (int i = 0; i < 24; i++) {
        int lx = 8 + i * 15;
        circle(lx, 46, 2, ((i + (int)(t * 8)) % 3) ? rgb(90, 60, 0) : rgb(255, 255, 160));
    }
    text_shadow(&fsml, "bitcircus101.de", (W - text_w(&fsml, "bitcircus101.de", 1)) / 2, 268, 1, rgb(255, 255, 255));
}

const Scene scene_juggle = {"juggle", 12, NULL, frame};

// SPDX-License-Identifier: GPL-3.0-or-later
// VHS/data-corruption look: RGB-split logo, slice tears, noise bursts, typed URL.
#include "core.h"

#include <string.h>

#define LS 4

static void glyph_add(int ch, int x, int y, uint16_t c) {
    for (int gy = 0; gy < fbig.h; gy++)
        for (int gx = 0; gx < fbig.w; gx++)
            if (glyph_px(&fbig, ch, gx, gy))
                for (int dy = 0; dy < LS; dy++)
                    for (int dx = 0; dx < LS; dx++) {
                        int X = x + gx * LS + dx, Y = y + gy * LS + dy;
                        if ((unsigned)X < W && (unsigned)Y < H) buf[Y * W + X] = add16(buf[Y * W + X], c);
                    }
}

static void frame(float t, float dt) {
    (void)dt;
    clear(rgb(4, 4, 10));
    unsigned period = (unsigned)(t / 0.9f);
    int burst = fmodf(t, 0.9f) < 0.2f && ((period * 2654435761u) >> 28) % 3 != 0;
    int dx = burst ? 4 + (int)(rnd() % 10) : 2;

    const char *logo = "BC101";
    int x0 = (W - 5 * fbig.w * LS) / 2, y0 = 40;
    for (int i = 0; i < 5; i++) {
        int x = x0 + i * fbig.w * LS, ch = (unsigned char)logo[i];
        glyph_add(ch, x - dx, y0, rgb(255, 0, 0));
        glyph_add(ch, x, y0 + (burst ? (int)(rnd() % 5) - 2 : 0), rgb(0, 255, 0));
        glyph_add(ch, x + dx, y0, rgb(0, 0, 255));
    }

    const char *url = "bitcircus101.de";
    int len = strlen(url), n = (int)((t - 1.0f) * 12);
    if (n < 0) n = 0;
    if (n > len) n = len;
    char tmp[32];
    memcpy(tmp, url, n);
    tmp[n] = 0;
    if (burst && n > 0) tmp[rnd() % n] = (char)(33 + rnd() % 90);
    int ux = (W - text_w(&fbig, url, 1)) / 2;
    text(&fbig, tmp, ux, 200, 1, rgb(0, 255, 200));
    if ((int)(t * 4) & 1) rect(ux + n * fbig.w, 228, 14, 4, rgb(0, 255, 200));

    if (burst) {
        uint16_t tmprow[W];
        for (int k = 0; k < 6; k++) {
            int ys = rnd() % H, hh = 2 + rnd() % 16, sh = (int)(rnd() % 61) - 30;
            for (int y = ys; y < ys + hh && y < H; y++) {
                uint16_t *row = buf + y * W;
                for (int x = 0; x < W; x++) tmprow[(x + sh + W) % W] = row[x];
                memcpy(row, tmprow, sizeof tmprow);
            }
        }
        for (int k = 0; k < 800; k++) {
            int g = rnd() & 255;
            buf[rnd() % (W * H)] = rgb(g, g, g);
        }
        for (int k = 0; k < 3; k++) rect(rnd() % W, rnd() % H, 20 + rnd() % 80, 2 + rnd() % 4, hsv16(frand(), 1, 1));
    }

    int band = (int)fmodf(t * 60, H);
    for (int y = 0; y < H; y++) {
        uint16_t *row = buf + y * W;
        if (y & 1)
            for (int x = 0; x < W; x++) row[x] = scale16(row[x], 170);
        if (y >= band && y < band + 6)
            for (int x = 0; x < W; x++) row[x] = add16(row[x], rgb(24, 24, 24));
    }
}

const Scene scene_glitch = {"glitch", 8, NULL, frame};

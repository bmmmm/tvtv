// SPDX-License-Identifier: GPL-3.0-or-later
// Old-school sine plasma with a huge "bc101" cut out of it (inverted palette, outline, shadow).
#include "core.h"

#include <string.h>

#define LOGO "bc101"
#define LS 4

static int8_t st[256];
static uint16_t pal[256];
static uint8_t mask[W * H]; // 0 none, 1 letter, 2 outline, 3 shadow
static int ready;

static void init(void) {
    if (ready) return;
    ready = 1;
    for (int i = 0; i < 256; i++) {
        float a = i * 2 * PI / 256;
        st[i] = (int8_t)(127 * sinf(a));
        pal[i] = rgb(128 + (int)(100 * sinf(a)), 128 + (int)(100 * sinf(a + 2.1f)), 128 + (int)(100 * sinf(a + 4.2f)));
    }
    memset(mask, 0, sizeof mask);
    int n = strlen(LOGO), x0 = (W - n * fbig.w * LS) / 2, y0 = (H - fbig.h * LS) / 2 - 20;
    for (int i = 0; i < n; i++)
        for (int gy = 0; gy < fbig.h; gy++)
            for (int gx = 0; gx < fbig.w; gx++)
                if (glyph_px(&fbig, (unsigned char)LOGO[i], gx, gy))
                    for (int dy = 0; dy < LS; dy++)
                        for (int dx = 0; dx < LS; dx++) {
                            int x = x0 + (i * fbig.w + gx) * LS + dx, y = y0 + gy * LS + dy;
                            if ((unsigned)x < W && (unsigned)y < H) mask[y * W + x] = 1;
                        }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (mask[y * W + x]) continue;
            int edge = 0;
            for (int dy = -2; dy <= 2 && !edge; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if ((unsigned)xx < W && (unsigned)yy < H && mask[yy * W + xx] == 1) { edge = 1; break; }
                }
            if (edge) mask[y * W + x] = 2;
            else if (x >= 7 && y >= 7 && mask[(y - 7) * W + x - 7] == 1) mask[y * W + x] = 3;
        }
}

static void frame(float t, float dt) {
    (void)dt;
    int a = (int)(t * 70), b = (int)(t * 50), c = (int)(t * 35), d = (int)(t * 90), rot = (int)(t * 40);
    int bob = (int)(sinf(t * 1.5f) * 12);
    for (int y = 0; y < H; y++) {
        uint16_t *row = buf + y * W;
        int my = y - bob;
        const uint8_t *mrow = (my >= 0 && my < H) ? mask + my * W : NULL;
        int ry = st[(y + a) & 255];
        for (int x = 0; x < W; x++) {
            int v = st[(x + b) & 255] + ry + st[(((x + y) >> 1) + c) & 255] + st[(st[((x >> 1) + d) & 255] + y + c) & 255];
            int idx = ((v >> 2) + rot) & 255;
            int m = mrow ? mrow[x] : 0;
            row[x] = m == 0 ? pal[idx] : m == 1 ? pal[(idx + 128) & 255] : m == 2 ? rgb(16, 8, 28) : scale16(pal[idx], 90);
        }
    }
    text_shadow(&fbig, "bitcircus101.de", (W - text_w(&fbig, "bitcircus101.de", 1)) / 2, 238, 1, rgb(255, 255, 255));
}

const Scene scene_plasma = {"plasma", 10, init, frame};

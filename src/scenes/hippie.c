// Hypnotic rainbow spiral, rotating peace sign, tall wobbly 70s lettering.
#include "core.h"

#include <string.h>

static int8_t st[256];
static uint16_t pal[256];
static int ready;

static void init(void) {
    polar_init();
    if (ready) return;
    ready = 1;
    for (int i = 0; i < 256; i++) {
        st[i] = (int8_t)(127 * sinf(i * 2 * PI / 256));
        float stripe = 0.6f + 0.4f * fabsf(sinf(i * PI * 6 / 256));
        pal[i] = hsv16(i / 256.0f, 0.85f, stripe);
    }
}

// Glyphs stretched 2x vertically, every row shifted by a sine for a melting look.
static void wobble_text(const char *s, int y0, float t) {
    int n = strlen(s), x0 = (W - n * fbig.w) / 2;
    for (int i = 0; i < n; i++)
        for (int gy = 0; gy < fbig.h; gy++) {
            int y = y0 + gy * 2;
            int off = (int)(sinf(y * 0.12f + t * 4) * 5);
            uint16_t c = hsv16(y / 90.0f - t * 0.5f, 0.8f, 1.0f);
            for (int gx = 0; gx < fbig.w; gx++)
                if (glyph_px(&fbig, (unsigned char)s[i], gx, gy)) {
                    int x = x0 + i * fbig.w + gx + off;
                    px(x + 2, y + 2, rgb(30, 0, 40));
                    px(x + 2, y + 3, rgb(30, 0, 40));
                    px(x, y, c);
                    px(x, y + 1, c);
                }
        }
}

static void fat_line(float x0, float y0, float x1, float y1, uint16_t c) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) line((int)x0 + dx, (int)y0 + dy, (int)x1 + dx, (int)y1 + dy, c);
}

static void frame(float t, float dt) {
    (void)dt;
    int sp = (int)(t * 160), wb = (int)(t * 50);
    for (int i = 0; i < W * H; i++) {
        int d = pdist[i];
        buf[i] = pal[(d * 3 + pang[i] * 2 - sp + (st[(d * 2 + wb) & 255] >> 3)) & 255];
    }
    int cx = W / 2, cy = H / 2 + 10, R = 58;
    float phi = sinf(t * 0.8f) * 0.6f;
    uint16_t white = rgb(255, 255, 255);
    circle(cx, cy, R + 3, rgb(30, 0, 40));
    circle(cx, cy, R - 4, scale16(pal[(int)(t * 60) & 255], 120));
    for (int r = R - 4; r <= R + 2; r++) ring(cx, cy, r, white);
    float s = sinf(phi), c = cosf(phi), q = 0.7071f * (R - 3);
    fat_line(cx + s * (R - 3), cy - c * (R - 3), cx - s * (R - 3), cy + c * (R - 3), white);
    fat_line(cx, cy, cx + (-q * c - q * s), cy + (-q * s + q * c), white);
    fat_line(cx, cy, cx + (q * c - q * s), cy + (q * s + q * c), white);
    wobble_text("PEACE & LOVE", 2, t);
    text_shadow(&fsml, "bitcircus101.de", (W - text_w(&fsml, "bitcircus101.de", 1)) / 2, 268, 1, white);
}

const Scene scene_hippie = {"hippie", 10, init, frame};

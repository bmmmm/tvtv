// Texture-mapped tunnel flight; "BC101" is stamped into the wall texture.
#include "core.h"

#include <string.h>

#define LW (2 * W)
#define LH (2 * H)

static uint8_t lu[LW * LH], lv[LW * LH], ls[LW * LH];
static uint16_t tex[4][256 * 256];
static int ready;

static void init(void) {
    if (ready) return;
    ready = 1;
    for (int y = 0; y < LH; y++)
        for (int x = 0; x < LW; x++) {
            float dx = x - LW / 2.0f + 0.5f, dy = y - LH / 2.0f + 0.5f;
            float d = sqrtf(dx * dx + dy * dy);
            int i = y * LW + x;
            lu[i] = (uint8_t)((int)(atan2f(dy, dx) * 256 / (2 * PI)) & 255);
            lv[i] = (uint8_t)((int)(6000.0f / (d + 1.0f)) & 255);
            ls[i] = d < 25 ? 0 : d < 50 ? 1 : d < 90 ? 2 : 3;
        }
    uint16_t *t3 = tex[3];
    for (int v = 0; v < 256; v++)
        for (int u = 0; u < 256; u++) {
            float lum = 0.4f + 0.4f * ((u ^ v) & 31) / 31.0f;
            t3[v * 256 + u] = (((u >> 5) + (v >> 5)) & 1) ? hsv16(0.83f, 0.8f, lum) : hsv16(0.53f, 0.8f, lum);
        }
    const char *word = "BC101";
    for (int rep = 0; rep < 3; rep++)
        for (int row = 0; row < 2; row++)
            for (int i = 0; i < 5; i++)
                for (int gy = 0; gy < fbig.h; gy++)
                    for (int gx = 0; gx < fbig.w; gx++)
                        if (glyph_px(&fbig, (unsigned char)word[i], gx, gy)) {
                            int u = (rep * 85 + 2 + i * fbig.w + gx) & 255, v = (16 + row * 128 + gy) & 255;
                            t3[v * 256 + u] = rgb(255, 255, 220);
                        }
    static const int lvl[3] = {40, 100, 170};
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < 256 * 256; i++) tex[k][i] = scale16(t3[i], lvl[k]);
}

static void frame(float t, float dt) {
    (void)dt;
    int ox = W / 2 + (int)(sinf(t * 0.8f) * W * 0.3f);
    int oy = H / 2 + (int)(sinf(t * 0.6f + 1) * H * 0.3f);
    int du = (int)(t * 30), dv = (int)(t * 180);
    for (int y = 0; y < H; y++) {
        int base = (y + oy) * LW + ox;
        const uint8_t *U = lu + base, *V = lv + base, *S = ls + base;
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++)
            row[x] = tex[S[x]][(((V[x] + dv) & 255) << 8) | ((U[x] + du) & 255)];
    }
    if (t > 1.0f) text_shadow(&fbig, "BITCIRCUS101", (W - text_w(&fbig, "BITCIRCUS101", 1)) / 2, 12, 1, rgb(255, 255, 255));
}

const Scene scene_tunnel = {"tunnel", 10, init, frame};

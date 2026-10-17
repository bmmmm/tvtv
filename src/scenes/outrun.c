// SPDX-License-Identifier: GPL-3.0-or-later
// Synthwave: striped sunset, scrolling neon grid, rotating 3D point-cloud "BC101".
#include "core.h"

#define HZ 170
#define MAXPT 3000

static float pts[MAXPT][3];
static int npts;

static void init(void) {
    if (npts) return;
    const char *word = "BC101";
    for (int li = 0; li < 5; li++)
        for (int gy = 0; gy < fbig.h; gy += 2)
            for (int gx = 0; gx < fbig.w; gx += 2)
                if (glyph_px(&fbig, (unsigned char)word[li], gx, gy))
                    for (int zl = -1; zl <= 1 && npts < MAXPT; zl++) {
                        pts[npts][0] = (li * fbig.w + gx - 40) * 2.0f;
                        pts[npts][1] = (gy - 16) * 2.0f;
                        pts[npts][2] = zl * 7.0f;
                        npts++;
                    }
}

static void frame(float t, float dt) {
    (void)dt;
    for (int y = 0; y < H; y++) {
        uint16_t c;
        if (y < HZ) {
            float k = (float)y / HZ;
            c = rgb((int)(20 + 180 * k * k), (int)(40 * k * k), (int)(40 + 80 * k));
        } else {
            c = rgb(10, 0, 20);
        }
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
    int scx = 180, scy = 118, sr = 55;
    for (int y = scy - sr; y < HZ && y <= scy + sr; y++) {
        int dy = y - scy;
        if (dy > 0 && (dy % 10) < dy / 8) continue;
        int dx = (int)sqrtf((float)(sr * sr - dy * dy));
        float k = (float)(dy + sr) / (2 * sr);
        uint16_t c = rgb(255, (int)(220 - 160 * k), (int)(60 + 80 * k));
        for (int x = scx - dx; x <= scx + dx; x++) px(x, y, c);
    }
    for (int i = 0; i < 24; i++) {
        float z = i + 1 - fmodf(t * 2.0f, 1.0f);
        int y = HZ + (int)(140.0f / z);
        if (y <= HZ || y >= H) continue;
        int k = (int)(256 * fminf(1.0f, 2.0f / z));
        line(0, y, W - 1, y, scale16(rgb(255, 40, 200), k));
    }
    for (int xi = -12; xi <= 12; xi++) line(180 + xi * 8, HZ, 180 + xi * 90, H - 1, rgb(160, 20, 130));

    float a = t * 1.1f, b = sinf(t * 0.7f) * 0.35f;
    float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
    int bob = (int)(sinf(t * 2) * 6);
    for (int i = 0; i < npts; i++) {
        float X = pts[i][0], Y = pts[i][1], Z = pts[i][2];
        float x = X * ca + Z * sa, z = -X * sa + Z * ca;
        float y = Y * cb - z * sb, z2 = Y * sb + z * cb;
        float zz = z2 + 200, k = 200 / zz;
        int sx = 180 + (int)(x * k), sy = 78 + (int)(y * k) + bob;
        float v = fminf(1.0f, fmaxf(0.3f, 0.5f + 0.5f * (1 - (zz - 140) / 120)));
        rect(sx, sy, 2, 2, hsv16(0.5f + 0.08f * sinf(t + X * 0.02f), 0.6f, v));
    }
    text_shadow(&fsml, "bitcircus101.de", (W - text_w(&fsml, "bitcircus101.de", 1)) / 2, 268, 1, rgb(0, 255, 255));
}

const Scene scene_outrun = {"outrun", 12, init, frame};

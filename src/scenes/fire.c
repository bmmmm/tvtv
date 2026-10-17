// Doom-style fire at half resolution; the title itself burns, then the fire dies down.
#include "core.h"

#include <string.h>

#define FW (W / 2)
#define FH (H / 2)

static uint8_t F[FW * FH];
static uint16_t pal[37];
static const uint8_t P[37][3] = {
    {7, 7, 7}, {31, 7, 7}, {47, 15, 7}, {71, 15, 7}, {87, 23, 7}, {103, 31, 7}, {119, 31, 7}, {143, 39, 7},
    {159, 47, 7}, {175, 63, 7}, {191, 71, 7}, {199, 71, 7}, {223, 79, 7}, {223, 87, 7}, {223, 87, 7},
    {215, 95, 7}, {215, 95, 7}, {215, 103, 15}, {207, 111, 15}, {207, 119, 15}, {207, 127, 15},
    {207, 135, 23}, {199, 135, 23}, {199, 143, 23}, {199, 151, 31}, {191, 159, 31}, {191, 159, 31},
    {191, 167, 39}, {191, 167, 39}, {191, 175, 47}, {183, 175, 47}, {183, 183, 47}, {183, 183, 55},
    {207, 207, 111}, {223, 223, 159}, {239, 239, 199}, {255, 255, 255},
};

#define TITLE "BITCIRCUS101"

static void init(void) {
    memset(F, 0, sizeof F);
    for (int i = 0; i < 37; i++) pal[i] = rgb(P[i][0], P[i][1], P[i][2]);
}

static void frame(float t, float dt) {
    (void)dt;
    int alive = t < 5.5f;
    for (int x = 0; x < FW; x++) F[(FH - 1) * FW + x] = alive ? 36 : 0;
    if (alive && t > 0.5f) {
        int n = strlen(TITLE), x0 = (FW - n * fsml.w) / 2, y0 = 76;
        for (int i = 0; i < n; i++)
            for (int gy = 0; gy < fsml.h; gy++)
                for (int gx = 0; gx < fsml.w; gx++)
                    if (glyph_px(&fsml, (unsigned char)TITLE[i], gx, gy)) F[(y0 + gy) * FW + x0 + i * fsml.w + gx] = 36;
    }
    for (int x = 0; x < FW; x++)
        for (int y = 1; y < FH; y++) {
            int src = y * FW + x, p = F[src];
            if (!p) { F[src - FW] = 0; continue; }
            int r = rnd() & 3, dst = src - r + 1 - FW;
            if (dst >= 0 && dst < FW * FH) F[dst] = p - (r & 1);
        }
    for (int y = 0; y < FH; y++) {
        uint16_t *r0 = buf + 2 * y * W, *r1 = r0 + W;
        for (int x = 0; x < FW; x++) {
            uint16_t c = pal[F[y * FW + x]];
            r0[2 * x] = r0[2 * x + 1] = r1[2 * x] = r1[2 * x + 1] = c;
        }
    }
    if (t > 1.0f) text_shadow(&fbig, "bitcircus101.de", (W - text_w(&fbig, "bitcircus101.de", 1)) / 2, 24, 1, rgb(255, 230, 150));
}

const Scene scene_fire = {"fire", 8, init, frame};

// SPDX-License-Identifier: GPL-3.0-or-later
// Digital rain; drops that cross the hidden logo lock in place until "BC101" stands.
#include "core.h"

#include <string.h>

#define CW 8
#define CHH 16
#define GC (W / CW)
#define GR (H / CHH)

static float head[GC], speed[GC], inten[GC][GR];
static char ch[GC][GR];
static uint8_t lock[GC][GR], logo[GC][GR];
static const char cs[] = "0123456789ABCDEFbc#$%&*+=<>@";
#define NCS (int)(sizeof cs - 1)

static void init(void) {
    memset(inten, 0, sizeof inten);
    memset(lock, 0, sizeof lock);
    memset(logo, 0, sizeof logo);
    for (int c = 0; c < GC; c++) {
        head[c] = -frand() * GR;
        speed[c] = 6 + frand() * 10;
        for (int r = 0; r < GR; r++) ch[c][r] = cs[rnd() % NCS];
    }
    const char *word = "BC101";
    int c0 = (GC - 40) / 2, r0 = (GR - 8) / 2;
    for (int li = 0; li < 5; li++)
        for (int cy = 0; cy < 8; cy++)
            for (int cx = 0; cx < 8; cx++) {
                int on = 0;
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 2; dx++)
                        on |= glyph_px(&fbig, (unsigned char)word[li], cx * 2 + dx, cy * 4 + dy);
                if (on) logo[c0 + li * 8 + cx][r0 + cy] = 1;
            }
}

static void frame(float t, float dt) {
    clear(rgb(0, 0, 0));
    for (int c = 0; c < GC; c++) {
        float old = head[c];
        head[c] += speed[c] * dt;
        for (int r = (int)floorf(old) + 1; r <= (int)floorf(head[c]); r++)
            if (r >= 0 && r < GR) {
                inten[c][r] = 1;
                ch[c][r] = cs[rnd() % NCS];
                if (t > 3.0f && logo[c][r]) lock[c][r] = 1;
            }
        if (head[c] > GR + 8) {
            head[c] = -frand() * 10;
            speed[c] = 6 + frand() * 10;
        }
    }
    for (int c = 0; c < GC; c++)
        for (int r = 0; r < GR; r++) {
            if (t > 7.5f && logo[c][r]) lock[c][r] = 1;
            uint16_t col;
            if (lock[c][r]) {
                if (rnd() % 40 == 0) ch[c][r] = cs[rnd() % NCS];
                col = rgb(180, 255, 210);
            } else {
                inten[c][r] -= dt * 0.9f;
                if (inten[c][r] <= 0.02f) { inten[c][r] = 0; continue; }
                if (rnd() % 30 == 0) ch[c][r] = cs[rnd() % NCS];
                float i = inten[c][r];
                col = r == (int)floorf(head[c]) ? rgb(220, 255, 220) : rgb(0, 40 + (int)(200 * i), (int)(40 * i));
            }
            glyph(&fsml, (unsigned char)ch[c][r], c * CW, r * CHH, 1, col);
        }
}

const Scene scene_matrix = {"matrix", 10, init, frame};

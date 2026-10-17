// SPDX-License-Identifier: GPL-3.0-or-later
// Bernd das Brot bounces around the screen like the DVD screensaver logo and changes
// colour on every wall hit. The crossing times are chosen so that he hits the top left
// corner dead-on at t = 10 s.
#include "core.h"

#include <stddef.h>

#define SC 2 // sprite scale
#define SPW 30
#define SPH 32
#define TX 2.6f // seconds per horizontal crossing
#define TY 1.9f // seconds per vertical crossing
#define PX 0.4f // phase offsets: both crossings end together at t = 10
#define PY 1.4f

// . transparent, # crust, = dark crust, l highlight, w eye white, k black
// (a row longer than SPW is a compile error, a shorter one is padded with NUL = transparent)
static const char sprite[SPH][SPW + 1] = {
    "...........========...........",
    ".........==lll#####==.........",
    ".......==ll##########==.......",
    "......=################=......",
    ".....=##################=.....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=###=====####=====###=....",
    "....=###=====####=====###=....",
    "....=###wwwww####wwwww###=....",
    "....=###wkkww####wkkww###=....",
    "....=###wkkww####wkkww###=....",
    "....=###wwwww####wwwww###=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "====######kkkkkkkkkk######====",
    "=#######kk##########kk#######=",
    "=######k##############k######=",
    "====######################====",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....=####################=....",
    "....======================....",
};

// bread first, then the DVD logo colours
static const uint8_t pal[][3] = {
    {214, 160, 78}, {255, 70, 220}, {70, 220, 255}, {255, 230, 70},
    {90, 230, 100}, {255, 140, 50}, {100, 130, 255}, {235, 235, 235},
};
#define NPAL (int)(sizeof pal / sizeof pal[0])

static float tri(float x) {
    x = fmodf(x, 2.0f);
    return x < 1 ? x : 2 - x;
}

static void frame(float t, float dt) {
    (void)dt;
    clear(rgb(0, 0, 0));
    float fx = (t + PX) / TX, fy = (t + PY) / TY;
    int x0 = (int)(tri(fx) * (W - SPW * SC) + 0.5f), y0 = (int)(tri(fy) * (H - SPH * SC) + 0.5f);
    const uint8_t *c = pal[((int)fx + (int)fy) % NPAL];
    uint16_t body = rgb(c[0], c[1], c[2]), dark = rgb(c[0] * 3 / 5, c[1] * 3 / 5, c[2] * 3 / 5);
    uint16_t light = rgb(c[0] + 50, c[1] + 50, c[2] + 50), white = rgb(255, 255, 255), black = rgb(10, 8, 8);
    for (int r = 0; r < SPH; r++)
        for (int col = 0; col < SPW; col++) {
            uint16_t p;
            switch (sprite[r][col]) {
            case '#': p = body; break;
            case '=': p = dark; break;
            case 'l': p = light; break;
            case 'w': p = white; break;
            case 'k': p = black; break;
            default: continue;
            }
            rect(x0 + col * SC, y0 + r * SC, SC, SC, p);
        }
}

const Scene scene_bernd = {"bernd", 13, NULL, frame};

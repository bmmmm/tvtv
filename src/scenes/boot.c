// SPDX-License-Identifier: GPL-3.0-or-later
// Fake retro BIOS boot that types itself out, then reveals the logo.
#include "core.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *s;
    int kind; // 0 text, 1 memory counter, 2 progress bar
    int pause;
} Line;

static const Line lines[] = {
    {"BC101 BIOS v1.01  (c) bitcircus101", 0, 10},
    {"", 0, 4},
    {"CPU: ARM1176JZF-S @ 700 MHz", 0, 8},
    {"Memory test: ", 1, 50},
    {"Detecting clowns .......... 3 found", 0, 10},
    {"Detecting juggling balls .. 5 found", 0, 10},
    {"Loading CIRCUS.SYS ", 2, 60},
    {"Inflating big top ......... OK", 0, 10},
    {"Calibrating confetti ...... OK", 0, 14},
    {"", 0, 4},
    {"> WELCOME TO BITCIRCUS101", 0, 20},
};
#define NLINES (int)(sizeof lines / sizeof lines[0])

static void frame(float t, float dt) {
    (void)dt;
    clear(rgb(0, 0, 0));
    uint16_t amber = rgb(255, 176, 0);
    int budget = (int)(t * 42);
    int curx = 8, cury = 8, done = 0;
    for (int i = 0; i < NLINES; i++) {
        const Line *l = &lines[i];
        int y = 8 + i * 16, len = strlen(l->s);
        int shown = budget < len ? budget : len;
        char tmp[64];
        memcpy(tmp, l->s, shown);
        tmp[shown] = 0;
        text(&fsml, tmp, 8, y, 1, amber);
        budget -= shown;
        curx = 8 + shown * 8;
        cury = y;
        if (budget <= 0) break;
        int used = budget < l->pause ? budget : l->pause;
        if (l->kind == 1) {
            char n[32];
            snprintf(n, sizeof n, "%d KB%s", used * 524288 / l->pause, used == l->pause ? " OK" : "");
            text(&fsml, n, curx, y, 1, amber);
            curx += strlen(n) * 8;
        } else if (l->kind == 2) {
            int bw = 120;
            rect(curx, y + 3, bw, 10, rgb(60, 40, 0));
            rect(curx, y + 3, used * bw / l->pause, 10, amber);
            curx += bw + 4;
        }
        budget -= used;
        if (budget <= 0) break;
        if (i == NLINES - 1) done = 1;
    }
    if (!done && ((int)(t * 3) & 1)) rect(curx, cury + 2, 8, 13, amber);
    if (done) {
        const char *logo = "BITCIRCUS101";
        int n = strlen(logo), x0 = (W - n * fbig.w) / 2;
        for (int i = 0; i < n; i++)
            glyph(&fbig, (unsigned char)logo[i], x0 + i * fbig.w, 220, 1, hsv16(i / (float)n + t * 0.5f, 0.8f, 1.0f));
    }
}

const Scene scene_boot = {"boot", 10, NULL, frame};

// SPDX-License-Identifier: GPL-3.0-or-later
// Overscan test card: numbered lines along every edge. Left/right numbers are in
// framebuffer pixels (canvas x * 2), top/bottom numbers are in lines.
#include "core.h"

#include <stdio.h>

#define STEPS 13
#define STEP 4

static void frame(float t, float dt) {
    (void)t;
    (void)dt;
    clear(rgb(0, 0, 0));
    for (int k = 0; k < STEPS; k++) {
        uint16_t c = hsv16(k / (float)STEPS, 0.8f, 1.0f);
        int d = k * STEP;
        rect(0, d, W, 1, c);
        rect(0, H - 1 - d, W, 1, c);
        rect(d, 0, 1, H, c);
        rect(W - 1 - d, 0, 1, H, c);
    }
    for (int k = 0; k < STEPS; k++) {
        uint16_t c = hsv16(k / (float)STEPS, 0.8f, 1.0f);
        int d = k * STEP;
        char s[8];
        snprintf(s, sizeof s, "%d", d);
        text(&fsml, s, 70 + k * 18, d + 1, 1, c);
        text(&fsml, s, 70 + k * 18, H - 1 - d - 16, 1, c);
        snprintf(s, sizeof s, "%d", d * 2);
        text(&fsml, s, d + 2, 56 + k * 14, 1, c);
        text(&fsml, s, W - 1 - d - 1 - text_w(&fsml, s, 1), 56 + k * 14, 1, c);
    }
    ring(W / 2, H / 2, 60, rgb(255, 255, 255));
    rect(W / 2 - 70, H / 2, 141, 1, rgb(255, 255, 255));
    rect(W / 2, H / 2 - 70, 1, 141, rgb(255, 255, 255));
    text_shadow(&fbig, "OVERSCAN", (W - text_w(&fbig, "OVERSCAN", 1)) / 2, H / 2 - 52, 1, rgb(255, 255, 255));
}

const Scene scene_testcard = {"testcard", 3600, NULL, frame, NULL};

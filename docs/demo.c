// SPDX-License-Identifier: GPL-3.0-or-later
// A visual for the remote's upload path: scripts/tv upload docs/demo.c -> channel 30 "demo".
// Any scene works the same way: include core.h, draw into buf[] in frame(), end with the Scene.
#include "core.h"

static void frame(float t, float dt) {
    (void)dt;
    clear(rgb(0, 0, 40));
    int y = (int)(H / 2 + sinf(t * 2) * 100);
    rect(0, y - 4, W, 8, hsv16(t * 0.2f, 1, 1));
    text_shadow(&fbig, "DEMO", (W - text_w(&fbig, "DEMO", 1)) / 2, 24, 1, rgb(255, 255, 255));
}

const Scene scene_demo = {"demo", 6, NULL, frame, NULL};

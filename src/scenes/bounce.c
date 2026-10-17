// SPDX-License-Identifier: GPL-3.0-or-later
// Bouncing rainbow title over raster bars, sine scroller along the bottom.
#include "core.h"

#include <string.h>

#define TITLE "BITCIRCUS101"

static void frame(float t, float dt) {
    (void)dt;
    raster_bars(t, 0.8f);
    int n = strlen(TITLE), x0 = (W - n * fbig.w) / 2;
    float amp = 30.0f * fminf(1.0f, t);
    for (int i = 0; i < n; i++) {
        int x = x0 + i * fbig.w;
        int y = 70 + (int)(sinf(t * 3.0f + i * 0.6f) * amp);
        glyph(&fbig, (unsigned char)TITLE[i], x + 2, y + 2, 1, rgb(10, 10, 30));
        glyph(&fbig, (unsigned char)TITLE[i], x, y, 1, hsv16(i / (float)n + t * 0.2f, 0.7f, 1.0f));
    }
    scroller("   BITCIRCUS101  ///  bitcircus101.de  ///  BC101  ///  hello from a raspberry pi 1   ", t, 232, 18);
}

const Scene scene_bounce = {"bounce", 8, NULL, frame};

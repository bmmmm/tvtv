// SPDX-License-Identifier: GPL-3.0-or-later
// Channel 24, the pixel wall: 64 x 48 pixels in the 16 colours of the C64, set by anyone with a
// post to the bridge's account ("px 12 30 rot", the bridge's docs/einwurf.md) or with
// `ssh zapper px`. A pixel is a 5x5 cell; rulers and faint lines every 8 pixels help a viewer to
// find x and y. The newest pixel blinks for a while after it appears and its author is named. The
// wall is live: what arrives while it is on air shows at once. Without a snapshot: KEIN EMPFANG.
#include "core.h"
#include "social.h"

#include <stdio.h>
#include <string.h>

#define CELL 5
#define WX 29 // the wall's top left corner, 320 x 240 from there
#define WY 22
#define FY 267 // the footer line
#define BLINK 10.0f // seconds a new pixel blinks
#define NAMED 5.0f  // and has its author in the footer; after that the invitation and the
#define SWAP 5.0f   // colour legend take turns there

// The C64's colours as Pepto measured them (the bridge's docs/einwurf.md, 2.1; VICE and FFmpeg
// use the same).
static const uint8_t c64[16][3] = {
    {0x00, 0x00, 0x00}, {0xff, 0xff, 0xff}, {0x68, 0x37, 0x2b}, {0x70, 0xa4, 0xb2},
    {0x6f, 0x3d, 0x86}, {0x58, 0x8d, 0x43}, {0x35, 0x28, 0x79}, {0xb8, 0xc7, 0x6f},
    {0x6f, 0x4f, 0x25}, {0x43, 0x39, 0x00}, {0x9a, 0x67, 0x59}, {0x44, 0x44, 0x44},
    {0x6c, 0x6c, 0x6c}, {0x9a, 0xd2, 0x84}, {0x6c, 0x5e, 0xb5}, {0x95, 0x95, 0x95},
};

static uint16_t pal[16];
static uint32_t newest;     // the newest pixel's event when the wall last looked, across runs
static float since = -1e9f; // and the scene time it appeared at

static void init(void) {
    for (int i = 0; i < 16; i++) pal[i] = rgb(c64[i][0], c64[i][1], c64[i][2]);
    const Social *s = social_get();
    since = -1e9f;
    if (s && s->nrecent && s->recent[0].event != newest) {
        newest = s->recent[0].event;
        since = 0;
    }
}

static float length(void) {
    const Social *s = social_get();
    return s ? (float)s->pixel_secs : SOCIAL_CARD_SECS;
}

// A quarter of the way to mid grey: a line that shows on dark and on light pixels alike.
static uint16_t faint(uint16_t c) {
    int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    return (uint16_t)(((r * 3 + 16) / 4) << 11 | ((g * 3 + 32) / 4) << 5 | (b * 3 + 16) / 4);
}

static int light(int i) { return c64[i][0] * 3 + c64[i][1] * 6 + c64[i][2] > 1300; }

static void centred(const char *s, int y, uint16_t c) { text_shadow(&fsml, s, (W - text_w(&fsml, s, 1)) / 2, y, 1, c); }

static void legend(void) {
    for (int i = 0; i < 16; i++) {
        char n[4];
        snprintf(n, sizeof n, "%d", i);
        int x = WX + i * 20;
        rect(x + 1, FY, 18, 16, pal[11]); // an edge, for black on black
        rect(x + 2, FY + 1, 16, 14, pal[i]);
        text(&fsml, n, x + 10 - text_w(&fsml, n, 1) / 2, FY, 1, light(i) ? 0 : 0xFFFF);
    }
}

static void frame(float t, float dt) {
    (void)dt;
    const Social *s = social_get();
    if (!s) {
        social_card(t, 24, "pixel");
        return;
    }
    if (s->nrecent && s->recent[0].event != newest) {
        newest = s->recent[0].event;
        since = t;
    }
    clear(0);
    rect(WX - 2, WY - 2, SOCIAL_W * CELL + 4, SOCIAL_H * CELL + 4, pal[14]); // the C64's border colour
    for (int y = 0; y < SOCIAL_H; y++)
        for (int x = 0; x < SOCIAL_W; x++) rect(WX + x * CELL, WY + y * CELL, CELL, CELL, pal[s->canvas[y][x]]);
    for (int k = 1; k < SOCIAL_W / 8; k++)
        for (int y = WY; y < WY + SOCIAL_H * CELL; y++) buf[y * W + WX + k * 8 * CELL] = faint(buf[y * W + WX + k * 8 * CELL]);
    for (int k = 1; k < SOCIAL_H / 8; k++)
        for (int x = WX; x < WX + SOCIAL_W * CELL; x++) buf[(WY + k * 8 * CELL) * W + x] = faint(buf[(WY + k * 8 * CELL) * W + x]);
    uint16_t ruler = rgb(170, 160, 240); // the border's light blue, lighter: small digits on a small tube
    char n[64];
    for (int k = 0; k < SOCIAL_W / 8; k++) {
        snprintf(n, sizeof n, "%d", k * 8);
        text(&fsml, n, WX + k * 8 * CELL + 1, WY - 20, 1, ruler);
    }
    for (int k = 0; k < SOCIAL_H / 8; k++) {
        snprintf(n, sizeof n, "%d", k * 8);
        text(&fsml, n, WX - 4 - text_w(&fsml, n, 1), WY + k * 8 * CELL, 1, ruler);
    }

    const SocialPixel *p = s->nrecent ? &s->recent[0] : NULL;
    int fresh = p && t - since < BLINK;
    if (fresh) {
        // a frame around it, white and black in turn, and marks on the border at its row and column
        int on = (int)(t * 4) & 1, cx = WX + p->x * CELL, cy = WY + p->y * CELL;
        uint16_t a = on ? 0xFFFF : 0, b = on ? 0 : 0xFFFF;
        rect(cx - 2, cy - 2, CELL + 4, CELL + 4, b);
        rect(cx - 1, cy - 1, CELL + 2, CELL + 2, a);
        rect(cx, cy, CELL, CELL, pal[s->canvas[p->y][p->x]]);
        rect(cx, WY - 5, CELL, 5, a);
        rect(WX - 5, cy, 5, CELL, a);
    }

    if (s->flags & SOCIAL_PAUSED) {
        const char *msg = " nimmt gerade nichts an";
        int w = text_w(&fsml, "PAUSE", 1) + 8 + text_w(&fsml, msg, 1), x = (W - w) / 2;
        rect(x, FY, text_w(&fsml, "PAUSE", 1) + 8, 16, rgb(255, 220, 0));
        text(&fsml, "PAUSE", x + 4, FY, 1, 0);
        text_shadow(&fsml, msg, x + text_w(&fsml, "PAUSE", 1) + 8, FY, 1, 0xFFFF);
    } else if (p && t - since < NAMED) {
        snprintf(n, sizeof n, "%.38s  %d %d", p->author, p->x, p->y);
        int w = 16 + text_w(&fsml, n, 1), x = (W - w) / 2;
        rect(x, FY + 2, 12, 12, 0xFFFF);
        rect(x + 1, FY + 3, 10, 10, pal[p->color]);
        text_shadow(&fsml, n, x + 16, FY, 1, rgb(255, 230, 120));
    } else if ((int)((t - (since >= 0 ? since + NAMED : 0)) / SWAP) & 1) {
        legend();
    } else {
        snprintf(n, sizeof n, "%.44s px X Y FARBE", s->bot);
        centred(n, FY, 0xFFFF);
    }
}

const Scene scene_pixel = {"pixel", SOCIAL_CARD_SECS, init, frame, length};

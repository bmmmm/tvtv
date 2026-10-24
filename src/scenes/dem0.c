// SPDX-License-Identifier: GPL-3.0-or-later
// Channel 25, dem0: picture code from public posts ("code sin(x/9+t)*99", the bridge's
// docs/dem0.md). One program per run: the announced one, else the next older one, so that a held
// 25 goes round all of them. The VM (dem0.c) computes the picture; on top of it the author, all
// the time (and PAUSE while the bridge takes nothing), and the source for the first seconds.
// Without a program: KEIN EMPFANG.
#include "core.h"
#include "dem0.h"
#include "social.h"

#include <stdio.h>
#include <string.h>

#define SOURCE_SECS 6.0f
#define COLS 44

static SocialProg prog; // a copy: the snapshot may change while it plays
static int have, nlines;
static char lines[3][COLS + 1], author[48];

// The source in at most 3 lines of 44 columns, broken after a space or ; where one is near the
// end of a line; what does not fit ends in "...".
static void wrap(const char *src) {
    nlines = 0;
    size_t len = strlen(src);
    while (*src && nlines < 3) {
        size_t n = len < COLS ? len : COLS;
        if (n < len) {
            size_t cut = n;
            while (cut > COLS - 12 && src[cut - 1] != ' ' && src[cut - 1] != ';') cut--;
            if (cut > COLS - 12) n = cut;
        }
        if (nlines == 2 && n < len) {
            n = COLS - 3;
            memcpy(lines[2], src, n);
            memcpy(lines[2] + n, "...", 4);
        } else {
            memcpy(lines[nlines], src, n);
            lines[nlines][n] = 0;
        }
        nlines++;
        src += n;
        len -= n;
        while (*src == ' ') {
            src++;
            len--;
        }
    }
}

static void init(void) {
    const SocialProg *p = social_next_prog();
    have = p != NULL;
    if (!have) return;
    prog = *p;
    dem0_start(&prog.prog);
    wrap(prog.source);
    if (strlen(prog.author) > 42) snprintf(author, sizeof author, "%.39s...", prog.author);
    else snprintf(author, sizeof author, "%s", prog.author);
}

static float length(void) { return have ? (float)prog.secs : SOCIAL_CARD_SECS; }

// Darkens a band of the picture, so that text on it reads whatever the program draws.
static void shade(int y, int h) {
    for (int i = y * W; i < (y + h) * W; i++) buf[i] = scale16(buf[i], 72);
}

static void frame(float t, float dt) {
    (void)dt;
    if (!have) {
        social_card(t, 25, "dem0");
        return;
    }
    dem0_render(t);
    shade(H - 34, 24);
    text_shadow(&fsml, author, 12, H - 30, 1, rgb(255, 230, 120));
    const Social *s = social_get();
    if (s && (s->flags & SOCIAL_PAUSED)) {
        int w = text_w(&fsml, "PAUSE", 1) + 8;
        rect(W - 12 - w, H - 30, w, 16, rgb(255, 220, 0));
        text(&fsml, "PAUSE", W - 8 - w, H - 30, 1, 0);
    }
    if (t < SOURCE_SECS) {
        shade(8, nlines * 16 + 8);
        for (int i = 0; i < nlines; i++) text_shadow(&fsml, lines[i], 4, 12 + i * 16, 1, rgb(140, 255, 170));
    }
}

const Scene scene_dem0 = {"dem0", SOCIAL_CARD_SECS, init, frame, length};

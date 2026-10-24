// SPDX-License-Identifier: GPL-3.0-or-later
// Channel 26, the text page: a page like those of German teletext (Videotext), with the lines the
// operator set up in the bridge, each under its label, and what people wrote into them
// ("text 2 Hallo", the bridge's docs/einwurf.md): the header with page number and clock, a title
// in double height, the lines, the invitation at the foot (while the bridge pauses: that it does).
// More lines than fit on the screen go on subpages that take turns, as on teletext. Without a
// snapshot: KEIN EMPFANG.
#include "core.h"
#include "social.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define COLS 44 // of 8 pixels, from X0
#define X0 4
#define TOP 74  // the first line of the page body
#define ROWS 11 // body lines per subpage
#define TITLE "SCHWARZES BRETT"

// teletext's colours, at full strength
#define T_WHITE rgb(255, 255, 255)
#define T_YELLOW rgb(255, 255, 0)
#define T_CYAN rgb(0, 255, 255)
#define T_GREEN rgb(0, 255, 0)
#define T_BLUE rgb(0, 0, 255)
#define T_RED rgb(255, 0, 0)

typedef struct {
    int rows, wrapped, author_row; // author_row: the author does not fit next to the label
    char line[2][COLS + 1];
} Lay;

static float len_s = SOCIAL_CARD_SECS;

static void init(void) {
    const Social *s = social_get();
    len_s = s ? (float)s->text_secs : SOCIAL_CARD_SECS;
}

static float length(void) { return len_s; }

static void put(const char *s, int col, int row_y, uint16_t c) { text(&fsml, s, X0 + col * 8, row_y, 1, c); }

// Teletext's double height: every row of the small font twice.
static void tall(const char *s, int x, int y, uint16_t c) {
    for (; *s; s++, x += fsml.w)
        for (int gy = 0; gy < fsml.h; gy++)
            for (int gx = 0; gx < fsml.w; gx++)
                if (glyph_px(&fsml, (unsigned char)*s, gx, gy)) rect(x + gx, y + gy * 2, 1, 2, c);
}

// A line's text in at most two rows of 42 columns, broken at a space where there is one.
static void layout(const Social *s, int i, Lay *l) {
    const SocialSlot *sl = &s->slot[i];
    memset(l, 0, sizeof *l);
    int head = 2 + (int)strlen(sl->label);
    l->author_row = sl->author[0] && head + 2 + (int)strlen(sl->author) > COLS;
    l->rows = 1 + l->author_row;
    const char *p = sl->text;
    for (int r = 0; r < 2 && *p; r++) {
        size_t n = strlen(p), max = COLS - 2;
        if (n > max) {
            size_t cut = max;
            while (cut > 0 && p[cut] != ' ') cut--;
            n = cut > max / 2 ? cut : max;
        }
        memcpy(l->line[r], p, n);
        l->line[r][n] = 0;
        p += n;
        while (*p == ' ') p++;
        l->rows++;
        l->wrapped = r + 1;
    }
}

static void draw_slot(const Social *s, int i, const Lay *l, int y) {
    const SocialSlot *sl = &s->slot[i];
    char n[16];
    snprintf(n, sizeof n, "%d", i + 1);
    put(n, 0, y, T_YELLOW);
    put(sl->label, 2, y, T_CYAN);
    if (!sl->text[0]) {
        // "-- frei --", the dashes drawn: Latin-1 has none of that length
        int col = 2 + (int)strlen(sl->label) + 2, x = X0 + col * 8;
        rect(x, y + 8, 12, 2, T_WHITE);
        put("frei", col + 2, y, T_WHITE);
        rect(x + 52, y + 8, 12, 2, T_WHITE);
        return;
    }
    if (!l->author_row) put(sl->author, COLS - (int)strlen(sl->author), y, T_GREEN);
    for (int r = 0; r < l->wrapped; r++) put(l->line[r], 2, y + 16 * (r + 1), T_WHITE);
    if (l->author_row) {
        char a[COLS + 1];
        snprintf(a, sizeof a, "%.*s", COLS - 2, sl->author);
        put(a, COLS - (int)strlen(a), y + 16 * (l->wrapped + 1), T_GREEN);
    }
}

static void frame(float t, float dt) {
    (void)dt;
    const Social *s = social_get();
    if (!s) {
        social_card(t, 26, "text");
        return;
    }
    clear(0);

    // the header: page number, name, date and clock
    static const char *const day[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char d[32], c[32];
    snprintf(d, sizeof d, "%s %02d.%02d.", day[tm.tm_wday % 7], tm.tm_mday, tm.tm_mon + 1);
    snprintf(c, sizeof c, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    put("260", 0, 8, T_WHITE);
    put("TEXT", 5, 8, T_YELLOW);
    put("tvtv", 10, 8, T_CYAN);
    put(d, COLS - 19, 8, T_WHITE);
    put(c, COLS - 8, 8, T_YELLOW);

    // the subpages: as many lines as fit, in page order
    Lay lay[SOCIAL_SLOTS];
    int first[SOCIAL_SLOTS + 1], pages = 0, used = ROWS;
    for (int i = 0; i < s->nslots; i++) {
        layout(s, i, &lay[i]);
        if (used + lay[i].rows > ROWS) {
            first[pages++] = i;
            used = 0;
        }
        used += lay[i].rows;
    }
    first[pages] = s->nslots;
    int page = pages > 1 ? (int)(t / (len_s / pages)) % pages : 0;

    rect(0, 28, W, 38, T_BLUE);
    tall(TITLE, (W - text_w(&fsml, TITLE, 1)) / 2, 31, T_YELLOW);
    if (pages > 1) {
        snprintf(d, sizeof d, "%d/%d", page + 1, pages);
        text(&fsml, d, W - 8 - text_w(&fsml, d, 1), 39, 1, T_WHITE);
    }

    if (!s->nslots) {
        put("Diese Seite hat noch keine Zeilen.", 2, TOP + 16, T_WHITE);
    } else {
        int y = TOP;
        for (int i = first[page]; i < first[page + 1]; i++) {
            draw_slot(s, i, &lay[i], y);
            y += 16 * lay[i].rows;
        }
    }

    // the foot: how to write a line, or that nothing is taken now
    char inv[96];
    if (s->flags & SOCIAL_PAUSED) {
        rect(0, 258, W, 22, T_RED);
        snprintf(inv, sizeof inv, "PAUSE: der Fernseher nimmt nichts an");
    } else {
        rect(0, 258, W, 22, T_BLUE);
        snprintf(inv, sizeof inv, "%s text <nr> <dein Text>", s->bot);
        if (text_w(&fsml, inv, 1) > W - 8) snprintf(inv, sizeof inv, "text <nr> <dein Text> an %.20s", s->bot);
    }
    text(&fsml, inv, (W - text_w(&fsml, inv, 1)) / 2, 261, 1, T_WHITE);
}

const Scene scene_text = {"text", SOCIAL_CARD_SECS, init, frame, length};

// SPDX-License-Identifier: GPL-3.0-or-later
// Slide decks (making-of, tech stack): text comes from UTF-8 data files, is typed out,
// and each slide gets a live demo strip at the bottom. File format:
//   # Title          starts a slide (max 22 chars)
//   @demo [arg]      selects the strip, e.g. "@chain Mac|Pi 1|TV" or "@term ls -l"
//   > code           body line rendered as code
//   anything else    body text (max 44 chars, max 10 lines)
#include "core.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SLIDE_T 12.0f
#define MAXS 16
#define MAXL 10
#define LEN 48
#define SY 216

typedef struct {
    char title[LEN], demo[16], arg[LEN], lines[MAXL][LEN];
    int nl;
} Slide;

typedef struct {
    const char *path; // relative to the executable
    Slide s[MAXS];
    int n, loaded;
} Deck;

static Deck making = {"slides/making-of.txt"}, stack = {"slides/stack.txt"};
static Deck *cur = &making;
static int8_t st[256];

#define PFW (W / 2)
#define PFH ((H - SY) / 2)
static uint8_t fire[PFW * PFH];

#define HGW 20
#define HGH 4
static uint8_t heat[HGW * HGH];
static float heat_next;

#define NBALL 60
static float bx[NBALL], by[NBALL], bvx[NBALL], bvy[NBALL];

static float cpu_pct, cpu_t = -1;
static unsigned long cpu_ticks;

static void copy(char *dst, const char *src, size_t n) {
    size_t l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

static void load(Deck *d) {
    if (d->loaded) return;
    d->loaded = 1;
    const char *path = asset(d->path);
    FILE *f = fopen(path, "r");
    if (!f) perror(path);
    char ln[256];
    while (f && fgets(ln, sizeof ln, f)) {
        ln[strcspn(ln, "\r\n")] = 0;
        utf8_to_latin1(ln);
        if (ln[0] == '#') {
            if (d->n >= MAXS) break;
            Slide *s = &d->s[d->n++];
            memset(s, 0, sizeof *s);
            const char *p = ln + 1;
            while (*p == ' ') p++;
            copy(s->title, p, LEN);
        } else if (d->n && ln[0] == '@') {
            Slide *s = &d->s[d->n - 1];
            char *sp = strchr(ln, ' ');
            if (sp) {
                *sp = 0;
                copy(s->arg, sp + 1, LEN);
            }
            copy(s->demo, ln + 1, sizeof s->demo);
        } else if (d->n && d->s[d->n - 1].nl < MAXL) {
            Slide *s = &d->s[d->n - 1];
            copy(s->lines[s->nl++], ln, LEN);
        }
    }
    if (f) fclose(f);
    if (!d->n) {
        d->n = 1;
        copy(d->s[0].title, "slides missing", LEN);
        copy(d->s[0].lines[0], d->path, LEN);
        d->s[0].nl = 1;
    }
}

static void common_init(void) {
    load(cur);
    for (int i = 0; i < 256; i++) st[i] = (int8_t)(127 * sinf(i * 2 * PI / 256));
    memset(fire, 0, sizeof fire);
    memset(heat, 0, sizeof heat);
    heat_next = 0;
    cpu_t = -1;
    for (int i = 0; i < NBALL; i++) {
        bx[i] = frand() * W;
        by[i] = SY + 4 + frand() * 40;
        bvx[i] = frand() * 160 - 80;
        bvy[i] = frand() * -120;
    }
}

static void init_making(void) { cur = &making; common_init(); }
static void init_stack(void) { cur = &stack; common_init(); }
static float length(void) { return cur->n * SLIDE_T; }

// ---------- helpers ----------

static void box(int x, int y, int w, int h, uint16_t c) {
    rect(x, y, w, 2, c);
    rect(x, y + h - 2, w, 2, c);
    rect(x, y, 2, h, c);
    rect(x + w - 2, y, 2, h, c);
}

static void label(const char *s, int cx, int y, uint16_t c) { text(&fsml, s, cx - text_w(&fsml, s, 1) / 2, y, 1, c); }

static uint16_t fire_col(int v) { return rgb(v * 8, (v - 12) * 9, (v - 28) * 30); }

static const uint16_t WHITE = 0xFFFF;

// ---------- demo strips ----------

static void demo_pipe(float t) {
    int y0 = SY + 26;
    const char *names[3] = {"Mac", "Pi 1", "TV"};
    for (int i = 0; i < 3; i++) {
        box(16 + i * 134, y0, 60, 30, hsv16(i / 3.0f + 0.1f, 0.7f, 1.0f));
        label(names[i], 46 + i * 134, y0 + 7, WHITE);
    }
    label("SSH", 113, y0 - 20, rgb(180, 180, 255));
    label("PAL", 247, y0 - 20, rgb(255, 200, 120));
    for (int a = 0; a < 2; a++) {
        int xs = 76 + a * 134;
        rect(xs, y0 + 14, 74, 2, rgb(120, 120, 140));
        for (int k = 0; k < 3; k++) {
            int x = xs + (int)fmodf(t * 50 + k * 25, 74);
            circle(x, y0 + 15, 3, a ? hsv16(t + k / 3.0f, 0.8f, 1.0f) : rgb(160, 160, 255));
        }
    }
}

// Boxes from "A|B|C", a dot travels along, the active box lights up.
static void demo_chain(float t, const char *arg) {
    char tmp[LEN], *parts[8];
    int n = 0;
    copy(tmp, arg, LEN);
    for (char *p = strtok(tmp, "|"); p && n < 8; p = strtok(NULL, "|")) parts[n++] = p;
    if (!n) return;
    int slot = W / n, bw = slot - 8, y0 = SY + 20, active = (int)(t * 1.5f) % n;
    for (int i = 0; i < n; i++) {
        int x = i * slot + 4;
        uint16_t c = hsv16(i / (float)n, 0.7f, 1.0f);
        if (i == active) rect(x, y0, bw, 30, scale16(c, 90));
        box(x, y0, bw, 30, c);
        label(parts[i], x + bw / 2, y0 + 7, WHITE);
        if (i + 1 < n) rect(x + bw, y0 + 14, 8, 2, rgb(150, 150, 170));
    }
    float pos = fmodf(t * 1.5f, (float)n);
    circle((int)(pos * slot) + 4, y0 + 44, 3, hsv16(t, 0.8f, 1.0f));
}

static void demo_bars(void) {
    static const uint8_t cb[7][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {255, 0, 0}, {0, 0, 255},
    };
    for (int i = 0; i < 7; i++) {
        int g = (cb[i][0] * 30 + cb[i][1] * 59 + cb[i][2] * 11) / 100;
        rect(8 + i * 24, SY + 6, 22, 44, rgb(g, g, g));
        rect(188 + i * 24, SY + 6, 22, 44, rgb(cb[i][0], cb[i][1], cb[i][2]));
    }
    label("NTSC", 92, SY + 54, rgb(200, 200, 200));
    label("PAL", 272, SY + 54, WHITE);
}

static void demo_pixels(float t) {
    for (int gy = 0; gy < fsml.h; gy++)
        for (int gx = 0; gx < fsml.w; gx++)
            if (glyph_px(&fsml, 'B', gx, gy)) rect(30 + gx * 4, SY + 4 + gy * 4, 3, 3, hsv16(gy / 16.0f + t * 0.3f, 0.8f, 1.0f));
    for (int k = 0; k < 9; k++) {
        uint16_t c = hsv16(k / 9.0f + t * 0.1f, 0.8f, 1.0f);
        rect(110 + k * 27, SY + 14, 12, 26, c);
        rect(110 + k * 27 + 13, SY + 14, 12, 26, c);
    }
    label("360 x2 = 720", 230, SY + 48, WHITE);
}

static void demo_fps(float t) {
    int c = (int)(t * 50) % 50;
    for (int k = 0; k < 50; k++) {
        int d = (c - k + 50) % 50;
        int v = d < 12 ? 256 - d * 18 : 40;
        rect(5 + k * 7, SY + 10, 6, 22, scale16(hsv16(k / 50.0f, 0.8f, 1.0f), v));
    }
    label("1 s = 50 x 20 ms", W / 2, SY + 42, WHITE);
}

static void demo_plasma(float t) {
    int a = (int)(t * 70), b = (int)(t * 50), c = (int)(t * 35);
    for (int y = SY; y < H; y++) {
        uint16_t *row = buf + y * W;
        int ry = st[(y * 2 + a) & 255];
        for (int x = 0; x < W; x++) {
            int v = st[(x + b) & 255] + ry + st[(((x + y) >> 1) + c) & 255];
            row[x] = hsv16(((v / 3 + a) & 255) / 256.0f, 0.8f, 0.9f);
        }
    }
}

static void demo_fire(void) {
    for (int x = 0; x < PFW; x++) fire[(PFH - 1) * PFW + x] = 36;
    for (int x = 0; x < PFW; x++)
        for (int y = 1; y < PFH; y++) {
            int src = y * PFW + x, p = fire[src];
            if (!p) { fire[src - PFW] = 0; continue; }
            int r = rnd() & 3, dst = src - r + 1 - PFW;
            if (dst >= 0) fire[dst] = p - (r & 1) * 2 > 0 ? p - (r & 1) * 2 : 0;
        }
    for (int y = 0; y < PFH; y++)
        for (int x = 0; x < PFW; x++) rect(x * 2, SY + y * 2, 2, 2, fire_col(fire[y * PFW + x]));
}

static void demo_particles(void) {
    float dt = 0.02f;
    for (int i = 0; i < NBALL; i++) {
        bvy[i] += 300 * dt;
        bx[i] += bvx[i] * dt;
        by[i] += bvy[i] * dt;
        if (by[i] > H - 4) {
            by[i] = H - 4;
            bvy[i] = -fabsf(bvy[i]) * 0.92f;
            if (bvy[i] > -60) bvy[i] = -150 - frand() * 60;
        }
        if (bx[i] < 2 || bx[i] > W - 3) bvx[i] = -bvx[i];
        if (by[i] < SY + 3) { by[i] = SY + 3; bvy[i] = fabsf(bvy[i]); }
        circle((int)bx[i], (int)by[i], 2, hsv16(i / (float)NBALL, 0.8f, 1.0f));
    }
}

static void demo_list(float t) {
    scroller("   boot  flowers  bounce  rainbow  explode  kaleido  plasma  stars  hippie  tunnel  "
             "matrix  juggle  fire  glitch  outrun   ",
             t, SY + 18, 8);
}

static void demo_rainbow(float t) {
    for (int x = 0; x < W; x++)
        for (int b = 0; b < 6; b++) {
            int y = SY + 10 + b * 8 + (int)(sinf(x * 0.05f + t * 3) * 5);
            rect(x, y, 1, 8, hsv16(b / 6.0f, 0.85f, 1.0f));
        }
}

// A linear array and the 2D grid it represents: i = y*8 + x.
static void demo_buf1d(float t) {
    int i = (int)(t * 4) % 32, gx = i % 8, gy = i / 8;
    for (int k = 0; k < 32; k++) {
        uint16_t c = hsv16(k / 32.0f, 0.6f, k == i ? 1.0f : 0.45f);
        rect(20 + k * 10, SY + 2, 9, 10, c);
        int cx = 200 + (k % 8) * 18, cy = SY + 20 + (k / 8) * 13;
        rect(cx, cy, 17, 12, c);
        if (k == i) {
            box(20 + k * 10 - 1, SY + 1, 11, 12, WHITE);
            box(cx - 1, cy - 1, 19, 14, WHITE);
        }
    }
    char s[32];
    snprintf(s, sizeof s, "x=%d y=%d", gx, gy);
    text(&fsml, s, 12, SY + 22, 1, WHITE);
    snprintf(s, sizeof s, "i=%d*8+%d=%d", gy, gx, i);
    text(&fsml, s, 12, SY + 42, 1, rgb(120, 255, 160));
}

// Paint row by row into an invisible buffer, then copy the finished picture at once.
static void demo_double(float t) {
    int cycle = (int)(t / 2.0f);
    float ph = fmodf(t, 2.0f), p = fminf(1.0f, ph / 1.5f);
    int bw = 120, bh = 46, lx = 20, rx = 220, y0 = SY + 20;
    label("buf[]", lx + bw / 2, SY + 2, rgb(200, 200, 255));
    label("/dev/fb0", rx + bw / 2, SY + 2, rgb(255, 220, 150));
    int rows = (int)(p * (bh - 4));
    for (int y = 0; y < rows; y++) rect(lx + 2, y0 + 2 + y, bw - 4, 1, hsv16(y / 40.0f + cycle * 0.3f, 0.8f, 1.0f));
    int shown = ph < 1.5f ? cycle - 1 : cycle;
    if (shown >= 0)
        for (int y = 0; y < bh - 4; y++) rect(rx + 2, y0 + 2 + y, bw - 4, 1, hsv16(y / 40.0f + shown * 0.3f, 0.8f, 1.0f));
    box(lx, y0, bw, bh, rgb(200, 200, 255));
    box(rx, y0, bw, bh, rgb(255, 220, 150));
    uint16_t ac = ph >= 1.5f ? WHITE : rgb(70, 70, 90);
    rect(lx + bw + 6, y0 + bh / 2, rx - lx - bw - 12, 3, ac);
    for (int k = 0; k < 6; k++) rect(rx - 12 + k, y0 + bh / 2 - 5 + k, 2, 13 - 2 * k, ac);
}

// One canvas pixel becomes two framebuffer pixels.
static void demo_blit(float t) {
    int k = (int)(t * 3) % 9;
    text(&fsml, "360", 4, SY + 8, 1, WHITE);
    text(&fsml, "720", 4, SY + 46, 1, WHITE);
    for (int i = 0; i < 9; i++) {
        uint16_t c = hsv16(i / 9.0f, 0.7f, i == k ? 1.0f : 0.6f);
        int sx = 48 + i * 32 + 8;
        rect(sx, SY + 6, 15, 15, c);
        rect(48 + i * 32, SY + 44, 15, 15, c);
        rect(48 + i * 32 + 16, SY + 44, 15, 15, c);
        if (i == k) {
            line(sx + 7, SY + 22, 48 + i * 32 + 7, SY + 43, WHITE);
            line(sx + 7, SY + 22, 48 + i * 32 + 23, SY + 43, WHITE);
            box(sx - 1, SY + 5, 17, 17, WHITE);
        }
    }
}

// Index buffer -> palette -> colour; rotating the palette recolours everything for free.
static void demo_palette(float t) {
    int rot = (int)(t * 40) & 255;
    for (int k = 0; k < 14; k++) {
        int v = (int)(128 + 127 * sinf(k * 0.45f));
        char s[8];
        snprintf(s, sizeof s, "%3d", v);
        int x = 5 + k * 25;
        text(&fsml, s, x, SY + 2, 1, WHITE);
        rect(x, SY + 20, 24, 16, hsv16(((v + rot) & 255) / 256.0f, 0.85f, 1.0f));
    }
    for (int x = 0; x < W; x++) rect(x, SY + 46, 1, 10, hsv16((((x * 256 / W) + rot) & 255) / 256.0f, 0.85f, 1.0f));
    char s[24];
    snprintf(s, sizeof s, "+%d", rot);
    text(&fsml, s, W - 40, SY + 56, 1, rgb(255, 220, 120));
}

// The fire buffer as numbers: heat rises, cools and drifts.
static void demo_heat(float t) {
    if (t >= heat_next) {
        heat_next = t + 0.15f;
        for (int x = 0; x < HGW; x++) heat[(HGH - 1) * HGW + x] = 30 + rnd() % 7;
        for (int y = 0; y < HGH - 1; y++)
            for (int x = 0; x < HGW; x++) {
                int sx = x + (int)(rnd() % 3) - 1;
                if (sx < 0) sx = 0;
                if (sx >= HGW) sx = HGW - 1;
                int v = heat[(y + 1) * HGW + sx] - (int)(rnd() % 9);
                heat[y * HGW + x] = v > 0 ? v : 0;
            }
    }
    for (int y = 0; y < HGH; y++)
        for (int x = 0; x < HGW; x++) {
            int v = heat[y * HGW + x];
            rect(x * 18, SY + y * 18, 17, 17, fire_col(v));
            char s[4];
            snprintf(s, sizeof s, "%d", v);
            text(&fsml, s, x * 18 + (v < 10 ? 5 : 1), SY + y * 18 + 1, 1, v > 26 ? rgb(0, 0, 0) : WHITE);
        }
}

static void demo_rgb565(float t) {
    uint32_t h = (uint32_t)(t / 1.5f) * 2654435761u;
    int r = h >> 24, g = (h >> 16) & 255, b = (h >> 8) & 255;
    uint16_t c = rgb(r, g, b);
    for (int i = 0; i < 16; i++) {
        int bit = (c >> (15 - i)) & 1;
        uint16_t bg = i < 5 ? rgb(150, 30, 30) : i < 11 ? rgb(30, 130, 40) : rgb(40, 60, 170);
        rect(10 + i * 21, SY + 4, 20, 18, bit ? bg : scale16(bg, 70));
        text(&fsml, bit ? "1" : "0", 10 + i * 21 + 6, SY + 5, 1, WHITE);
    }
    rect(10, SY + 28, 335, 18, c);
    char s[40];
    snprintf(s, sizeof s, "R=%d G=%d B=%d -> 0x%04X", r, g, b, c);
    label(s, W / 2, SY + 52, WHITE);
}

static void demo_sine(float t) {
    int phase = (int)(t * 60);
    rect(20, SY + 36, 320, 1, rgb(80, 80, 100));
    for (int i = 0; i < 64; i++) {
        int v = st[(i * 4 + phase) & 255] * 30 / 127;
        uint16_t c = hsv16(i / 64.0f, 0.7f, 1.0f);
        if (v >= 0) rect(20 + i * 5, SY + 36 - v, 4, v + 1, c);
        else rect(20 + i * 5, SY + 36, 4, -v, c);
    }
}

static void demo_term(float t, const char *arg) {
    rect(10, SY + 6, 340, 56, rgb(0, 10, 4));
    box(10, SY + 6, 340, 56, rgb(60, 140, 80));
    int len = strlen(arg), n = (int)(fmodf(t, 6.0f) * 15);
    if (n > len) n = len;
    char s[LEN + 4] = "$ ";
    memcpy(s + 2, arg, n);
    s[n + 2] = 0;
    text(&fsml, s, 18, SY + 18, 1, rgb(120, 255, 160));
    if ((int)(t * 3) & 1) rect(18 + (n + 2) * 8, SY + 20, 7, 13, rgb(120, 255, 160));
}

// Own CPU usage from /proc/self/stat, sampled once per second.
static void demo_cpu(float t) {
    if (cpu_t < 0 || t - cpu_t >= 1.0f) {
        FILE *f = fopen("/proc/self/stat", "r");
        unsigned long ut = 0, stt = 0;
        if (f) {
            char line[512];
            if (fgets(line, sizeof line, f)) {
                char *p = strrchr(line, ')');
                if (p) sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &stt);
            }
            fclose(f);
        }
        if (cpu_t >= 0) cpu_pct = (ut + stt - cpu_ticks) * 100.0f / sysconf(_SC_CLK_TCK) / (t - cpu_t);
        cpu_ticks = ut + stt;
        cpu_t = t;
    }
    int w = (int)(320 * fminf(1.0f, cpu_pct / 100.0f));
    rect(20, SY + 14, 320, 22, rgb(30, 30, 40));
    for (int x = 0; x < w; x++) rect(20 + x, SY + 14, 1, 22, hsv16(0.33f - 0.33f * x / 320.0f, 0.8f, 1.0f));
    for (int k = 1; k < 4; k++) rect(20 + k * 80, SY + 12, 1, 26, rgb(150, 150, 170));
    char s[24];
    snprintf(s, sizeof s, "CPU %.0f %%", cpu_pct);
    label(s, W / 2, SY + 44, WHITE);
}

// ---------- slide rendering ----------

static void frame(float t, float dt) {
    (void)dt;
    int si = (int)(t / SLIDE_T) % cur->n;
    float lt = fmodf(t, SLIDE_T);
    Slide *s = &cur->s[si];

    for (int y = 0; y < H; y++) {
        uint16_t c = rgb(8 + y / 16, 4, 28 + y / 10);
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
    for (int x = 0; x < W; x++) {
        uint16_t c = hsv16(x / (float)W - t * 0.3f, 0.8f, 1.0f);
        px(x, 44, c);
        px(x, 45, c);
    }
    int n = strlen(s->title), x0 = (W - n * fbig.w) / 2;
    for (int i = 0; i < n; i++) {
        int y = 6 + (int)(sinf(t * 3 + i * 0.5f) * 3);
        glyph(&fbig, (unsigned char)s->title[i], x0 + i * fbig.w + 2, y + 2, 1, rgb(0, 0, 0));
        glyph(&fbig, (unsigned char)s->title[i], x0 + i * fbig.w, y, 1, hsv16(i / (float)n + t * 0.2f, 0.6f, 1.0f));
    }

    int budget = (int)((lt - 0.4f) * 55), curx = -1, cury = 0;
    for (int l = 0; l < s->nl && budget > 0; l++) {
        const char *src = s->lines[l];
        int code = src[0] == '>';
        if (code) src += src[1] == ' ' ? 2 : 1;
        int len = strlen(src), shown = budget < len ? budget : len, y = 52 + l * 16;
        char tmp[LEN];
        memcpy(tmp, src, shown);
        tmp[shown] = 0;
        if (code) rect(0, y, W, 16, rgb(0, 24, 12));
        text(&fsml, tmp, 4, y, 1, code ? rgb(120, 255, 160) : rgb(240, 240, 255));
        budget -= len + 1;
        curx = 4 + shown * 8;
        cury = y;
    }
    if (curx >= 0 && budget <= 0 && ((int)(t * 3) & 1)) rect(curx, cury + 2, 7, 13, rgb(255, 200, 0));
    char pg[16];
    snprintf(pg, sizeof pg, "%d/%d", si + 1, cur->n);
    if (x0 + n * fbig.w < W - text_w(&fsml, pg, 1) - 8) text(&fsml, pg, W - text_w(&fsml, pg, 1) - 4, 26, 1, rgb(120, 110, 160));

    rect(0, SY - 2, W, H - SY + 2, rgb(0, 0, 0));
    const char *d = s->demo;
    if (!strcmp(d, "pipe")) demo_pipe(t);
    else if (!strcmp(d, "chain")) demo_chain(t, s->arg);
    else if (!strcmp(d, "bars")) demo_bars();
    else if (!strcmp(d, "pixels")) demo_pixels(t);
    else if (!strcmp(d, "fps")) demo_fps(t);
    else if (!strcmp(d, "plasma")) demo_plasma(t);
    else if (!strcmp(d, "fire")) demo_fire();
    else if (!strcmp(d, "particles")) demo_particles();
    else if (!strcmp(d, "list")) demo_list(t);
    else if (!strcmp(d, "rainbow")) demo_rainbow(t);
    else if (!strcmp(d, "buf1d")) demo_buf1d(t);
    else if (!strcmp(d, "double")) demo_double(t);
    else if (!strcmp(d, "blit")) demo_blit(t);
    else if (!strcmp(d, "palette")) demo_palette(t);
    else if (!strcmp(d, "heat")) demo_heat(t);
    else if (!strcmp(d, "rgb565")) demo_rgb565(t);
    else if (!strcmp(d, "sine")) demo_sine(t);
    else if (!strcmp(d, "term")) demo_term(t, s->arg);
    else if (!strcmp(d, "cpu")) demo_cpu(t);

    float f = fminf(1.0f, fminf(lt / 0.3f, (SLIDE_T - lt) / 0.3f));
    if (f < 1.0f) {
        int k = (int)(f * 256);
        for (int i = 0; i < W * H; i++) buf[i] = scale16(buf[i], k);
    }
}

const Scene scene_explain = {"explain", 0, init_making, frame, length};
const Scene scene_stack = {"stack", 0, init_stack, frame, length};

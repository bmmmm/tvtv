// Flower power: flowers sprout and bloom with spinning rainbow petals under a rotating sunburst.
#include "core.h"

#include <string.h>

#define NF 9
#define NPET 40
#define GROUND (H - 28)

typedef struct {
    float x, h, r, hue, delay, spin;
    int n;
} Flower;

static Flower fl[NF];
static float petx[NPET], pety[NPET], petv[NPET], peth[NPET];
static uint16_t rays[16];

static void init(void) {
    polar_init();
    for (int i = 0; i < 16; i++) rays[i] = hsv16(i / 16.0f, (i & 1) ? 0.25f : 0.45f, 0.95f);
    for (int i = 0; i < NF; i++) {
        Flower *f = &fl[i];
        f->x = 22 + i * 40 + frand() * 10;
        f->h = 60 + frand() * 110;
        f->r = 14 + frand() * 12;
        f->hue = frand();
        f->delay = frand() * 1.5f;
        f->spin = (0.5f + frand()) * ((rnd() & 1) ? 1 : -1);
        f->n = 5 + rnd() % 4;
    }
    for (int i = 0; i < NPET; i++) {
        petx[i] = frand() * W;
        pety[i] = -frand() * H;
        petv[i] = 20 + frand() * 30;
        peth[i] = frand();
    }
}

static float smooth(float x) {
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}

static void thick(int x0, int y0, int x1, int y1, uint16_t c) {
    for (int d = -1; d <= 1; d++) line(x0 + d, y0, x1 + d, y1, c);
}

static void frame(float t, float dt) {
    int rot = (int)(t * 20);
    for (int i = 0; i < W * H; i++) buf[i] = rays[((pang[i] + rot) & 255) >> 4];
    rect(0, GROUND, W, H - GROUND, rgb(40, 150, 40));
    for (int x = 0; x < W; x += 3) line(x, GROUND, x + (int)(sinf(t * 2 + x * 0.1f) * 2), GROUND - 4, rgb(60, 190, 60));

    for (int i = 0; i < NF; i++) {
        Flower *f = &fl[i];
        float g = smooth((t - f->delay) / 1.5f);
        if (g <= 0) continue;
        float sway = sinf(t * 1.5f + i) * 6 * g;
        int cx = (int)(f->x + sway), cy = (int)(GROUND - f->h * g);
        thick((int)f->x, GROUND, cx, cy, rgb(30, 120, 30));
        int my = (GROUND + cy) / 2;
        circle((int)f->x + (int)(sway / 2) + ((i & 1) ? 6 : -6), my, (int)(5 * g), rgb(50, 170, 50));
        float b = smooth((t - f->delay - 1.0f) / 1.0f);
        if (b <= 0) continue;
        float pr = f->r * b;
        for (int k = 0; k < f->n; k++) {
            float a = k * 2 * PI / f->n + t * f->spin;
            circle(cx + (int)(cosf(a) * pr * 0.9f), cy + (int)(sinf(a) * pr * 0.9f), (int)(pr * 0.55f),
                   hsv16(f->hue + (float)k / f->n + t * 0.1f, 0.75f, 1.0f));
        }
        circle(cx, cy, (int)(pr * 0.45f), rgb(255, 220, 40));
        circle(cx, cy, (int)(pr * 0.2f), rgb(255, 140, 20));
    }

    for (int i = 0; i < NPET; i++) {
        pety[i] += petv[i] * dt;
        if (pety[i] > GROUND) {
            pety[i] = -4;
            petx[i] = frand() * W;
        }
        int x = (int)(petx[i] + sinf(t * 2 + i) * 12);
        rect(x, (int)pety[i], 3, 2, hsv16(peth[i] + t * 0.2f, 0.7f, 1.0f));
    }

    const char *title = "FLOWER POWER";
    int n = strlen(title), x0 = (W - n * fbig.w) / 2;
    for (int i = 0; i < n; i++) {
        int y = 8 + (int)(sinf(t * 3 + i * 0.6f) * 5);
        glyph(&fbig, (unsigned char)title[i], x0 + i * fbig.w + 2, y + 2, 1, rgb(60, 0, 60));
        glyph(&fbig, (unsigned char)title[i], x0 + i * fbig.w, y, 1, hsv16(i / (float)n - t * 0.3f, 0.8f, 1.0f));
    }
    text_shadow(&fsml, "bitcircus101.de", (W - text_w(&fsml, "bitcircus101.de", 1)) / 2, 268, 1, rgb(255, 255, 255));
}

const Scene scene_flowers = {"flowers", 11, init, frame};

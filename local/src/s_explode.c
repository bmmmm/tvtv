// Title trembles, bursts into pixel particles with a shockwave, then reassembles.
#include "core.h"

#include <string.h>

#define TITLE "BITCIRCUS101"
#define TY 110
#define MAXP 4096
#define BOOM 0.8f

typedef struct {
    float x, y, vx, vy, hx, hy;
    uint16_t col;
    uint8_t ci;
} Particle;

static Particle p[MAXP];
static int np;

static void init(void) {
    np = 0;
    int n = strlen(TITLE), x0 = (W - n * fbig.w) / 2;
    float cx = W / 2.0f, cy = TY + fbig.h / 2.0f;
    for (int i = 0; i < n; i++)
        for (int gy = 0; gy < fbig.h; gy++)
            for (int gx = 0; gx < fbig.w; gx++)
                if (glyph_px(&fbig, (unsigned char)TITLE[i], gx, gy) && np < MAXP) {
                    Particle *q = &p[np++];
                    q->hx = q->x = x0 + i * fbig.w + gx;
                    q->hy = q->y = TY + gy;
                    q->vx = (q->hx - cx) * 2.2f + (frand() * 120 - 60);
                    q->vy = (q->hy - cy) * 2.2f - 140.0f + (frand() * 120 - 60);
                    q->col = hsv16((float)i / n, 0.85f, 0.7f + 0.3f * frand());
                    q->ci = i;
                }
}

static void frame(float t, float dt) {
    float tb = t - BOOM;
    raster_bars(t + 3, tb >= 0 && tb < 0.15f ? 2.0f : 0.6f);
    if (tb >= 0 && tb < 0.9f) {
        int cx = W / 2, cy = TY + fbig.h / 2, r = (int)(tb * 420);
        uint16_t c = scale16(rgb(255, 240, 200), (int)(256 * (1 - tb / 0.9f)));
        for (int k = 0; k < 3; k++) ring(cx, cy, r + k, c);
    }
    for (int i = 0; i < np; i++) {
        Particle *q = &p[i];
        if (tb < 0) {
            float j = t * 4.0f;
            px((int)(q->hx + sinf(t * 70 + q->ci * 1.7f) * j), (int)(q->hy + cosf(t * 53 + q->ci * 2.3f) * j), q->col);
            continue;
        }
        if (tb < 2.8f) {
            q->vy += 420.0f * dt;
            q->x += q->vx * dt;
            q->y += q->vy * dt;
            if (q->y > H - 2) { q->y = H - 2; q->vy *= -0.5f; q->vx *= 0.8f; }
            if (q->x < 0) { q->x = 0; q->vx = -q->vx; }
            if (q->x > W - 1) { q->x = W - 1; q->vx = -q->vx; }
        } else if (tb < 6.0f) {
            float k = 1.0f - expf(-dt * 4.0f);
            q->x += (q->hx - q->x) * k;
            q->y += (q->hy - q->y) * k;
        } else {
            q->x = q->hx;
            q->y = q->hy;
        }
        px((int)(q->x + 0.5f), (int)(q->y + 0.5f), q->col);
    }
}

const Scene scene_explode = {"explode", 8, init, frame};

// A rainbow paints itself across the sky; "BC101" flies through with a nyan-style rainbow trail.
#include "core.h"

#define ACX 180
#define ACY 300

static uint16_t rbc[W * H];
static uint8_t rba[W * H];
static int ready;
static const uint8_t bands[7][3] = {
    {255, 40, 40}, {255, 140, 0}, {255, 230, 0}, {40, 200, 60}, {40, 120, 255}, {90, 40, 200}, {170, 60, 220},
};
static const uint8_t trail[6][3] = {
    {255, 40, 40}, {255, 150, 0}, {255, 240, 0}, {50, 230, 50}, {40, 150, 255}, {130, 60, 230},
};

static void init(void) {
    if (ready) return;
    ready = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = x - ACX, dy = y - ACY, d = sqrtf(dx * dx + dy * dy);
            int band = (int)((d - 150) / 11);
            int i = y * W + x;
            rbc[i] = 0;
            if (d >= 150 && band < 7) {
                rbc[i] = rgb(bands[6 - band][0], bands[6 - band][1], bands[6 - band][2]);
                rba[i] = (uint8_t)(255 * (1 - atan2f(-dy, dx) / PI));
            }
        }
}

static void cloud(int x, int y, int s) {
    uint16_t c = rgb(250, 250, 255);
    circle(x, y, s, c);
    circle(x + s, y - s / 2, s, c);
    circle(x + 2 * s, y, s, c);
    circle(x + s, y + s / 3, s, c);
}

static void frame(float t, float dt) {
    (void)dt;
    for (int y = 0; y < H; y++) {
        float k = (float)y / H;
        uint16_t c = rgb((int)(90 + 120 * k), (int)(160 + 70 * k), 255);
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
    int reveal = (int)(255 * fminf(1.0f, t / 2.5f));
    for (int i = 0; i < W * H; i++)
        if (rbc[i] && rba[i] <= reveal) buf[i] = rbc[i];

    for (int i = 0; i < 4; i++) {
        int x = (int)fmodf(i * 110 + t * (12 + i * 4), W + 120) - 60;
        cloud(x, 40 + i * 37 % 90, 12 + i * 2);
    }

    if (t > 2.5f) {
        float tt = t - 2.5f;
        int head = -80 + (int)(tt * 75);
        int bob = (int)(sinf(tt * 5) * 10);
        int y0 = 140 + bob;
        for (int x = 0; x < head; x += 8) {
            int yo = ((x / 8 + (int)(t * 8)) & 1) ? 0 : 3;
            for (int b = 0; b < 6; b++)
                rect(x, y0 + yo + b * 4, 8, 4, rgb(trail[b][0], trail[b][1], trail[b][2]));
        }
        for (int i = 0; i < 5; i++) {
            int ch = (unsigned char)"BC101"[i];
            glyph(&fbig, ch, head + i * fbig.w + 2, y0 - 2, 1, rgb(40, 0, 60));
            glyph(&fbig, ch, head + i * fbig.w, y0 - 4, 1, hsv16(i / 5.0f + t, 0.5f, 1.0f));
        }
    }
    for (int i = 0; i < 12; i++) {
        uint32_t h = (uint32_t)(i * 2654435761u + (int)(t * 3) * 40503u);
        int x = h % W, y = (h >> 9) % 130;
        uint16_t c = rgb(255, 255, 200);
        px(x, y, c); px(x - 1, y, c); px(x + 1, y, c); px(x, y - 1, c); px(x, y + 1, c);
    }
    text_shadow(&fbig, "bitcircus101.de", (W - text_w(&fbig, "bitcircus101.de", 1)) / 2, 248, 1, rgb(255, 255, 255));
}

const Scene scene_rainbow = {"rainbow", 11, init, frame};

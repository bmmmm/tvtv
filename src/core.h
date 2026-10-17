// Shared canvas, fonts and drawing helpers for the bc101 demo scenes.
#pragma once
#include <math.h>
#include <stdint.h>

#define W 360
#define H 288
#define PI 3.14159265f

typedef struct {
    int w, h, bytes, n;
    uint8_t *data;
    uint16_t map[256]; // Latin-1 code point -> glyph index
} Font;

typedef struct {
    const char *name;
    float dur;
    void (*init)(void);
    void (*frame)(float t, float dt);
    float (*length)(void); // optional, overrides dur after init
} Scene;

extern uint16_t buf[W * H];
extern Font fbig, fsml; // 16x32 and 8x16 Terminus Bold

int asset_init(void);                  // remembers the executable's directory
const char *asset(const char *rel);    // path relative to it; valid until the next call
int load_font(Font *f, const char *path);
void utf8_to_latin1(char *s); // in place; unmappable code points become '?'

uint16_t rgb(int r, int g, int b);
void hsv(float h, float s, float v, int *r, int *g, int *b);
uint16_t hsv16(float h, float s, float v);
uint16_t add16(uint16_t a, uint16_t b);
uint16_t scale16(uint16_t c, int k); // k in 0..256
uint32_t rnd(void);
float frand(void);

static inline void px(int x, int y, uint16_t c) {
    if ((unsigned)x < W && (unsigned)y < H) buf[y * W + x] = c;
}

void clear(uint16_t c);
void rect(int x, int y, int w, int h, uint16_t c);
void line(int x0, int y0, int x1, int y1, uint16_t c);
void circle(int cx, int cy, int r, uint16_t c);
void ring(int cx, int cy, int r, uint16_t c);

int glyph_px(const Font *f, int ch, int x, int y);
void glyph(const Font *f, int ch, int x, int y, int s, uint16_t c);
void text(const Font *f, const char *str, int x, int y, int s, uint16_t c);
void text_shadow(const Font *f, const char *str, int x, int y, int s, uint16_t c);
int text_w(const Font *f, const char *str, int s);
void text_zoom(const Font *f, const char *str, float cx, float cy, float s, uint16_t c);

// Polar coordinates around the screen centre: angle 0..255 per turn, distance in pixels.
extern uint8_t pang[W * H];
extern uint16_t pdist[W * H];
void polar_init(void);

void raster_bars(float t, float dim);
void scroller(const char *str, float t, int y, float amp);

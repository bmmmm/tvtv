#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t buf[W * H];
Font fbig, fsml;

static void set_map(Font *f, int *seen, uint32_t cp, int g) {
    if (cp < 256 && !seen[cp] && g < f->n) {
        f->map[cp] = (uint16_t)g;
        seen[cp] = 1;
    }
}

int load_font(Font *f, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    uint8_t hd[32];
    int n, psf2 = 0, hastab;
    if (fread(hd, 1, 4, fp) != 4) { fclose(fp); return -2; }
    if (hd[0] == 0x36 && hd[1] == 0x04) { // PSF1
        f->w = 8;
        f->h = hd[3];
        f->bytes = hd[3];
        n = (hd[2] & 1) ? 512 : 256;
        hastab = hd[2] & 6;
    } else if (hd[0] == 0x72 && hd[1] == 0xb5 && hd[2] == 0x4a && hd[3] == 0x86) { // PSF2
        if (fread(hd + 4, 1, 28, fp) != 28) { fclose(fp); return -2; }
        uint32_t h32[8];
        memcpy(h32, hd, sizeof h32);
        n = h32[4];
        f->bytes = h32[5];
        f->h = h32[6];
        f->w = h32[7];
        hastab = h32[3] & 1;
        psf2 = 1;
        fseek(fp, h32[2], SEEK_SET);
    } else {
        fclose(fp);
        return -3;
    }
    f->n = n;
    f->data = malloc((size_t)n * f->bytes);
    if (!f->data || fread(f->data, f->bytes, n, fp) != (size_t)n) { fclose(fp); return -4; }

    int seen[256] = {0};
    if (hastab) {
        int g = 0, seq = 0, c;
        if (psf2) {
            while (g < n && (c = fgetc(fp)) != EOF) {
                if (c == 0xFF) { g++; seq = 0; continue; }
                if (c == 0xFE) { seq = 1; continue; }
                uint32_t cp = c;
                int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
                if (more) cp = c & (0x3F >> more);
                for (; more > 0; more--) cp = (cp << 6) | (fgetc(fp) & 0x3F);
                if (!seq) set_map(f, seen, cp, g);
            }
        } else {
            int lo, hi;
            while (g < n && (lo = fgetc(fp)) != EOF && (hi = fgetc(fp)) != EOF) {
                uint32_t v = (uint32_t)(lo | (hi << 8));
                if (v == 0xFFFF) { g++; seq = 0; continue; }
                if (v == 0xFFFE) { seq = 1; continue; }
                if (!seq) set_map(f, seen, v, g);
            }
        }
    }
    for (int i = 0; i < 256; i++)
        if (!seen[i]) f->map[i] = (uint16_t)(i < n ? i : '?');
    fclose(fp);
    return 0;
}

void utf8_to_latin1(char *s) {
    unsigned char *r = (unsigned char *)s, *w = r;
    while (*r) {
        unsigned c = *r++;
        if (c < 0x80) { *w++ = (unsigned char)c; continue; }
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        uint32_t cp = more ? c & (0x3F >> more) : '?';
        for (; more > 0 && (*r & 0xC0) == 0x80; more--) cp = (cp << 6) | (*r++ & 0x3F);
        *w++ = (unsigned char)(cp < 256 ? cp : '?');
    }
    *w = 0;
}

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

uint16_t rgb(int r, int g, int b) {
    r = clamp255(r);
    g = clamp255(g);
    b = clamp255(b);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void hsv(float h, float s, float v, int *r, int *g, int *b) {
    h -= floorf(h);
    float hh = h * 6.0f, i = floorf(hh), f = hh - i;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    float R, G, B;
    switch ((int)i % 6) {
    case 0: R = v; G = t; B = p; break;
    case 1: R = q; G = v; B = p; break;
    case 2: R = p; G = v; B = t; break;
    case 3: R = p; G = q; B = v; break;
    case 4: R = t; G = p; B = v; break;
    default: R = v; G = p; B = q; break;
    }
    *r = (int)(R * 255);
    *g = (int)(G * 255);
    *b = (int)(B * 255);
}

uint16_t hsv16(float h, float s, float v) {
    int r, g, b;
    hsv(h, s, v, &r, &g, &b);
    return rgb(r, g, b);
}

uint16_t add16(uint16_t a, uint16_t b) {
    int r = (a >> 11) + (b >> 11);
    int g = ((a >> 5) & 63) + ((b >> 5) & 63);
    int bl = (a & 31) + (b & 31);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (bl > 31) bl = 31;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t scale16(uint16_t c, int k) {
    int r = ((c >> 11) * k) >> 8;
    int g = (((c >> 5) & 63) * k) >> 8;
    int b = ((c & 31) * k) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t seed = 0x1b101u;

uint32_t rnd(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

float frand(void) { return (rnd() & 0xFFFFFF) / 16777216.0f; }

void clear(uint16_t c) {
    for (int i = 0; i < W * H; i++) buf[i] = c;
}

void rect(int x, int y, int w, int h, uint16_t c) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int yy = y; yy < y1; yy++)
        for (int xx = x; xx < x1; xx++) buf[yy * W + xx] = c;
}

void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        for (int x = cx - dx; x <= cx + dx; x++) px(x, cy + dy, c);
    }
}

void ring(int cx, int cy, int r, uint16_t c) {
    int steps = r * 7 + 16;
    for (int i = 0; i < steps; i++) {
        float a = i * 2 * PI / steps;
        px(cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), c);
    }
}

int glyph_px(const Font *f, int ch, int x, int y) {
    int rowbytes = (f->w + 7) / 8;
    ch = f->map[ch & 255];
    return (f->data[ch * f->bytes + y * rowbytes + x / 8] >> (7 - (x & 7))) & 1;
}

void glyph(const Font *f, int ch, int x, int y, int s, uint16_t c) {
    for (int gy = 0; gy < f->h; gy++)
        for (int gx = 0; gx < f->w; gx++)
            if (glyph_px(f, ch, gx, gy)) {
                if (s == 1) px(x + gx, y + gy, c);
                else rect(x + gx * s, y + gy * s, s, s, c);
            }
}

void text(const Font *f, const char *str, int x, int y, int s, uint16_t c) {
    for (; *str; str++, x += f->w * s) glyph(f, (unsigned char)*str, x, y, s, c);
}

void text_shadow(const Font *f, const char *str, int x, int y, int s, uint16_t c) {
    text(f, str, x + s + 1, y + s + 1, s, rgb(8, 4, 20));
    text(f, str, x, y, s, c);
}

int text_w(const Font *f, const char *str, int s) { return (int)strlen(str) * f->w * s; }

void text_zoom(const Font *f, const char *str, float cx, float cy, float s, uint16_t c) {
    if (s <= 0.01f) return;
    int len = strlen(str);
    float tw = len * f->w * s, th = f->h * s;
    float x0 = cx - tw / 2, y0 = cy - th / 2;
    int ya = (int)fmaxf(0, y0), yb = (int)fminf(H, y0 + th);
    int xa = (int)fmaxf(0, x0), xb = (int)fminf(W, x0 + tw);
    float inv = 1.0f / s;
    for (int y = ya; y < yb; y++) {
        int gy = (int)((y - y0) * inv);
        if (gy < 0 || gy >= f->h) continue;
        for (int x = xa; x < xb; x++) {
            int u = (int)((x - x0) * inv);
            int ci = u / f->w;
            if (ci < 0 || ci >= len) continue;
            if (glyph_px(f, (unsigned char)str[ci], u - ci * f->w, gy)) buf[y * W + x] = c;
        }
    }
}

uint8_t pang[W * H];
uint16_t pdist[W * H];

void polar_init(void) {
    static int ready;
    if (ready) return;
    ready = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = x - W / 2.0f + 0.5f, dy = y - H / 2.0f + 0.5f;
            pang[y * W + x] = (uint8_t)((int)(atan2f(dy, dx) * 256 / (2 * PI)) & 255);
            pdist[y * W + x] = (uint16_t)sqrtf(dx * dx + dy * dy);
        }
}

void raster_bars(float t, float dim) {
    for (int y = 0; y < H; y++) {
        float r = 0, g = 0, b = 0;
        for (int i = 0; i < 6; i++) {
            float cy = H / 2 + sinf(t * 1.1f + i * 0.7f) * 105.0f;
            float d = fabsf(y - cy);
            if (d < 14) {
                float k = 1.0f - d / 14.0f;
                int cr, cg, cb;
                hsv(i / 6.0f + t * 0.05f, 0.8f, 1.0f, &cr, &cg, &cb);
                r += cr * k;
                g += cg * k;
                b += cb * k;
            }
        }
        uint16_t c = rgb((int)(r * dim) + 4, (int)(g * dim) + 2, (int)(b * dim) + 10);
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
}

void scroller(const char *str, float t, int y, float amp) {
    int len = strlen(str);
    float off = fmodf(t * 80.0f, (float)(len * fbig.w + W));
    float x0 = W - off;
    for (int k = 0; k < len; k++) {
        int x = (int)(x0 + k * fbig.w);
        if (x < -fbig.w || x > W) continue;
        int yy = y + (int)(sinf(t * 4.0f + x * 0.03f) * amp);
        glyph(&fbig, (unsigned char)str[k], x + 2, yy + 2, 1, rgb(10, 10, 30));
        glyph(&fbig, (unsigned char)str[k], x, yy, 1, rgb(255, 255, 255));
    }
}

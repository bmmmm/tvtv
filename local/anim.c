// bc101 demo: bouncing title, raster bars, sine scroller, letter explosion.
// Renders a 360x288 canvas, doubles it horizontally onto a 720x288 RGB565 fbdev.
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W 360
#define H 288
#define MAXP 4096
#define CYCLE 16.0f
#define TITLE "BITCIRCUS101"
#define TITLE_Y 70
#define FONT_PATH "/home/deploy/tvtv/font.psf"

static uint16_t buf[W * H];
static volatile sig_atomic_t run = 1;

static uint8_t *glyphs;
static int gw, gh, gbytes;

typedef struct {
    float x, y, vx, vy, hx, hy;
    uint16_t col;
} Particle;
static Particle parts[MAXP];
static int nparts;

static void on_sig(int s) { (void)s; run = 0; }

static uint16_t rgb(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void hsv(float h, float s, float v, int *r, int *g, int *b) {
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
    *r = (int)(R * 255); *g = (int)(G * 255); *b = (int)(B * 255);
}

static uint16_t hsv16(float h, float s, float v) {
    int r, g, b;
    hsv(h, s, v, &r, &g, &b);
    return rgb(r, g, b);
}

static int load_font(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t hd[8];
    if (fread(hd, 4, 8, f) != 8 || hd[0] != 0x864ab572u) { fclose(f); return -2; }
    gh = hd[6]; gw = hd[7]; gbytes = hd[5];
    int len = hd[4];
    fseek(f, hd[2], SEEK_SET);
    glyphs = malloc((size_t)len * gbytes);
    if (fread(glyphs, gbytes, len, f) != (size_t)len) { fclose(f); return -3; }
    fclose(f);
    return 0;
}

static inline int glyph_px(int c, int x, int y) {
    int rowbytes = (gw + 7) / 8;
    return (glyphs[c * gbytes + y * rowbytes + x / 8] >> (7 - (x % 8))) & 1;
}

static inline void px(int x, int y, uint16_t c) {
    if ((unsigned)x < W && (unsigned)y < H) buf[y * W + x] = c;
}

static void draw_glyph(int ch, int x, int y, uint16_t c) {
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++)
            if (glyph_px(ch, gx, gy)) px(x + gx, y + gy, c);
}

static void raster_bars(float t, float dim) {
    for (int y = 0; y < H; y++) {
        float r = 0, g = 0, b = 0;
        for (int i = 0; i < 6; i++) {
            float cy = H / 2 + sinf(t * 1.1f + i * 0.7f) * 105.0f;
            float d = fabsf(y - cy);
            if (d < 14) {
                float k = 1.0f - d / 14.0f;
                int cr, cg, cb;
                hsv(i / 6.0f + t * 0.05f, 0.8f, 1.0f, &cr, &cg, &cb);
                r += cr * k; g += cg * k; b += cb * k;
            }
        }
        uint16_t c = rgb((int)(r * dim) + 4, (int)(g * dim) + 2, (int)(b * dim) + 10);
        uint16_t *row = buf + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }
}

static void init_particles(void) {
    nparts = 0;
    int n = strlen(TITLE);
    int x0 = (W - n * gw) / 2;
    for (int i = 0; i < n; i++)
        for (int gy = 0; gy < gh; gy++)
            for (int gx = 0; gx < gw; gx++)
                if (glyph_px((unsigned char)TITLE[i], gx, gy) && nparts < MAXP) {
                    Particle *p = &parts[nparts++];
                    p->hx = x0 + i * gw + gx;
                    p->hy = TITLE_Y + gy;
                    p->col = hsv16((float)i / n, 0.85f, 0.7f + 0.3f * (rand() % 100) / 100.0f);
                }
}

static void explode(void) {
    float cx = W / 2.0f, cy = TITLE_Y + gh / 2.0f;
    for (int i = 0; i < nparts; i++) {
        Particle *p = &parts[i];
        p->x = p->hx; p->y = p->hy;
        p->vx = (p->hx - cx) * 2.2f + (rand() % 121 - 60);
        p->vy = (p->hy - cy) * 2.2f - 120.0f + (rand() % 121 - 60);
    }
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    double limit = argc > 1 ? atof(argv[1]) : 0;
    int rc = load_font(FONT_PATH);
    if (rc) { fprintf(stderr, "font load failed (%d): %s\n", rc, FONT_PATH); return 1; }

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("/dev/fb0"); return 1; }
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    ioctl(fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fd, FBIOGET_FSCREENINFO, &fi);
    if (vi.bits_per_pixel != 16 || vi.xres != 2 * W || vi.yres != H) {
        fprintf(stderr, "unexpected fb: %ux%u %ubpp\n", vi.xres, vi.yres, vi.bits_per_pixel);
        return 1;
    }
    uint8_t *fb = mmap(NULL, (size_t)fi.line_length * vi.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    ioctl(0, KDSETMODE, KD_GRAPHICS);

    init_particles();
    srand(101);

    const char *scroll = "   BITCIRCUS101  ///  bitcircus101.de  ///  BC101  ///  hello from a raspberry pi 1   ";
    int slen = strlen(scroll);
    int n = strlen(TITLE);
    int tx0 = (W - n * gw) / 2;

    double start = now(), prev = start, next = start;
    int scene_prev = -1;
    while (run) {
        double tn = now();
        float t = (float)(tn - start);
        float dt = (float)(tn - prev);
        if (dt > 0.05f) dt = 0.05f;
        prev = tn;
        if (limit > 0 && t > limit) break;

        float ct = fmodf(t, CYCLE);
        int scene = ct < 8.0f ? 0 : 1;
        float sb = ct - 8.0f;
        if (scene == 1 && scene_prev != 1) explode();
        scene_prev = scene;

        raster_bars(t, scene == 1 && sb < 0.15f ? 2.0f : 0.8f);

        if (scene == 0) {
            float amp = 30.0f * fminf(1.0f, ct / 1.0f);
            for (int i = 0; i < n; i++) {
                int x = tx0 + i * gw;
                int y = TITLE_Y + (int)(sinf(t * 3.0f + i * 0.6f) * amp);
                draw_glyph((unsigned char)TITLE[i], x + 2, y + 2, rgb(10, 10, 30));
                draw_glyph((unsigned char)TITLE[i], x, y, hsv16(i / (float)n + t * 0.2f, 0.7f, 1.0f));
            }
        } else {
            for (int i = 0; i < nparts; i++) {
                Particle *p = &parts[i];
                if (sb < 2.8f) {
                    p->vy += 420.0f * dt;
                    p->x += p->vx * dt;
                    p->y += p->vy * dt;
                    if (p->y > H - 2) { p->y = H - 2; p->vy *= -0.5f; p->vx *= 0.8f; }
                    if (p->x < 0) { p->x = 0; p->vx = -p->vx; }
                    if (p->x > W - 1) { p->x = W - 1; p->vx = -p->vx; }
                } else if (sb < 6.0f) {
                    float k = 1.0f - expf(-dt * 4.0f);
                    p->x += (p->hx - p->x) * k;
                    p->y += (p->hy - p->y) * k;
                } else {
                    p->x = p->hx; p->y = p->hy;
                }
                px((int)(p->x + 0.5f), (int)(p->y + 0.5f), p->col);
            }
        }

        // sine scroller along the bottom
        float off = fmodf(t * 80.0f, (float)(slen * gw + W));
        float x0 = W - off;
        for (int k = 0; k < slen; k++) {
            int x = (int)(x0 + k * gw);
            if (x < -gw || x > W) continue;
            int y = 232 + (int)(sinf(t * 4.0f + x * 0.03f) * 18.0f);
            draw_glyph((unsigned char)scroll[k], x + 2, y + 2, rgb(10, 10, 30));
            draw_glyph((unsigned char)scroll[k], x, y, rgb(255, 255, 255));
        }

        for (int y = 0; y < H; y++) {
            uint32_t *dst = (uint32_t *)(fb + (size_t)y * fi.line_length);
            const uint16_t *src = buf + y * W;
            for (int x = 0; x < W; x++) dst[x] = src[x] | ((uint32_t)src[x] << 16);
        }

        next += 0.02;
        struct timespec ts = { (time_t)next, (long)((next - (time_t)next) * 1e9) };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        if (now() > next + 0.1) next = now();
    }

    memset(fb, 0, (size_t)fi.line_length * vi.yres);
    ioctl(0, KDSETMODE, KD_TEXT);
    munmap(fb, (size_t)fi.line_length * vi.yres);
    return 0;
}

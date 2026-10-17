// SPDX-License-Identifier: GPL-3.0-or-later
// Offscreen tool for scenes, no framebuffer needed; builds on the Mac and on the Pi (make preview).
//   build/preview [-f <fontdir>] [-1x] <scene> frames <outdir> <t>...     one BMP per time, 720x576
//   build/preview [-f <fontdir>] [-1x] <scene> seq <outdir> <t0> <t1> <fps>   numbered BMPs, e.g. for a flipbook
//   build/preview [-f <fontdir>] <scene> bench <frames>                   average and worst frame time
//   build/preview [-f <fontdir>] <scene> probe <t>...                     white centroid below y=50, non-black bbox
//   build/preview -l                                                      list scenes
// -1x writes the bare 360x288 canvas; the default doubles both axes, which is what a 4:3 tube shows.
// Fonts default to fonts/ in the current directory. On the Mac: sips -s format png x.bmp --out x.png
// frames and probe call frame(t) once per time: scenes that build their state frame by frame
// (stars, flowers, explode, matrix) need seq from 0. The sequencer's fade in and out is not applied.
#include "scenes.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int zoom = 2;

static void put16(FILE *f, int v) {
    fputc(v & 255, f);
    fputc((v >> 8) & 255, f);
}

static void put32(FILE *f, int v) {
    put16(f, v & 0xFFFF);
    put16(f, (v >> 16) & 0xFFFF);
}

static void write_bmp(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    int ow = zoom * W, oh = zoom * H, row = ow * 3;
    fputc('B', f);
    fputc('M', f);
    put32(f, 54 + row * oh);
    put32(f, 0);
    put32(f, 54);
    put32(f, 40);
    put32(f, ow);
    put32(f, oh);
    put16(f, 1);
    put16(f, 24);
    put32(f, 0);
    put32(f, row * oh);
    put32(f, 2835);
    put32(f, 2835);
    put32(f, 0);
    put32(f, 0);
    for (int y = oh - 1; y >= 0; y--)
        for (int x = 0; x < W; x++) {
            uint16_t c = buf[(y / zoom) * W + x];
            unsigned char bgr[3] = {(unsigned char)((c & 31) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                                    (unsigned char)((c >> 11) << 3)};
            for (int k = 0; k < zoom; k++) fwrite(bgr, 1, 3, f);
        }
    fclose(f);
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Where the pure white pixels below the title are (a marker such as the beam dot), and the
// extent of everything that is not black: enough to check motion over consecutive frames.
static void probe(const Scene *sc, float t) {
    sc->frame(t, 0.02f);
    long wx = 0, wy = 0, wn = 0;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint16_t c = buf[y * W + x];
            if (c == 0xFFFF && y > 50) { wx += x; wy += y; wn++; }
            if (c) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
        }
    printf("t=%6.2f  white %4ld px at %6.1f,%6.1f   non-black bbox %d,%d-%d,%d\n", t, wn,
           wn ? (double)wx / wn : -1, wn ? (double)wy / wn : -1, x0, y0, x1, y1);
}

static void outdir(const char *d) {
    if (mkdir(d, 0755) && errno != EEXIST) { perror(d); exit(1); }
}

static int usage(void) {
    fprintf(stderr, "usage: preview [-f fontdir] [-1x] <scene> frames <outdir> <t>... | seq <outdir> <t0> <t1> <fps>"
                    " | bench <frames> | probe <t>...\n       preview -l\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *fontdir = "fonts";
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-l")) { scene_list(); return 0; }
        if (!strcmp(argv[i], "-1x")) zoom = 1;
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fontdir = argv[++i];
        else return usage();
    }
    if (argc - i < 2) return usage();
    const Scene *sc = scene_find(argv[i]);
    if (!sc) { fprintf(stderr, "unknown scene: %s\n", argv[i]); return 1; }
    const char *mode = argv[i + 1];
    char **arg = argv + i + 2;
    int narg = argc - i - 2;

    char p[1024];
    snprintf(p, sizeof p, "%s/big.psf", fontdir);
    if (load_font(&fbig, p)) { fprintf(stderr, "cannot load %s\n", p); return 1; }
    snprintf(p, sizeof p, "%s/small.psf", fontdir);
    if (load_font(&fsml, p)) { fprintf(stderr, "cannot load %s\n", p); return 1; }
    if (sc->init) sc->init();
    float dur = sc->length ? sc->length() : sc->dur;

    if (!strcmp(mode, "frames") && narg >= 2) {
        outdir(arg[0]);
        for (int k = 1; k < narg; k++) {
            float t = (float)atof(arg[k]);
            sc->frame(t, 0.02f);
            snprintf(p, sizeof p, "%s/%s_%05.2f.bmp", arg[0], sc->name, t);
            write_bmp(p);
            puts(p);
        }
    } else if (!strcmp(mode, "seq") && narg == 4) {
        float t0 = (float)atof(arg[1]), t1 = (float)atof(arg[2]), fps = (float)atof(arg[3]);
        if (!(t1 > t0) || !(fps > 0)) return usage();
        outdir(arg[0]);
        int n = (int)((t1 - t0) * fps + 0.5f);
        for (int k = 0; k < n; k++) {
            sc->frame(t0 + k / fps, 1.0f / fps);
            snprintf(p, sizeof p, "%s/%s_%04d.bmp", arg[0], sc->name, k);
            write_bmp(p);
        }
        printf("%d frames in %s\n", n, arg[0]);
    } else if (!strcmp(mode, "bench") && narg == 1) {
        int n = atoi(arg[0]);
        if (n <= 0) return usage();
        double total = 0, worst = 0;
        for (int k = 0; k < n; k++) {
            double a = now();
            sc->frame(dur * k / n, 0.02f);
            double d = now() - a;
            total += d;
            if (d > worst) worst = d;
        }
        printf("%-8s avg %6.2f ms  worst %6.2f ms  over %d frames\n", sc->name, total / n * 1e3, worst * 1e3, n);
    } else if (!strcmp(mode, "probe") && narg >= 1) {
        for (int k = 0; k < narg; k++) probe(sc, (float)atof(arg[k]));
    } else {
        return usage();
    }
    return 0;
}

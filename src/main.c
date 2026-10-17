// SPDX-License-Identifier: GPL-3.0-or-later
// bc101 demo sequencer: plays scenes in a loop on a 720x288 RGB565 fbdev.
// usage: bc101 [-l] [-t seconds] [scene ...]
// Fonts and slides are looked up next to the executable (fonts/, slides/).
#include "core.h"
#include "scenes.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define FADE 0.4f

static volatile sig_atomic_t run = 1;
static void on_sig(int s) { (void)s; run = 0; }

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    double limit = 0;
    const Scene *sel[64];
    int nsel = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) {
            scene_list();
            return 0;
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            limit = atof(argv[++i]);
        } else {
            const Scene *sc = scene_find(argv[i]);
            if (!sc) { fprintf(stderr, "unknown scene: %s\n", argv[i]); return 1; }
            if (nsel >= 64) { fprintf(stderr, "too many scenes (max 64)\n"); return 1; }
            sel[nsel++] = sc;
        }
    }
    if (!nsel)
        for (int k = 0; k < scene_nloop; k++) sel[nsel++] = scene_loop[k];

    if (asset_init()) {
        perror("/proc/self/exe");
        return 1;
    }
    if (load_font(&fbig, asset("fonts/big.psf")) || load_font(&fsml, asset("fonts/small.psf"))) {
        fprintf(stderr, "font load failed in %s\n", asset("fonts/"));
        return 1;
    }

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
    size_t fbsize = (size_t)fi.line_length * vi.yres;
    uint8_t *fb = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }

    FILE *log = fopen("/tmp/anim.log", "w");
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    ioctl(0, KDSETMODE, KD_GRAPHICS);

    double start = now();
    for (int si = 0; run; si++) {
        const Scene *sc = sel[si % nsel];
        if (sc->init) sc->init();
        float dur = sc->length ? sc->length() : sc->dur;
        double s0 = now(), prev = s0, next = s0, worst = 0;
        int frames = 0;
        while (run) {
            double tn = now();
            float t = (float)(tn - s0);
            float dt = (float)(tn - prev);
            if (dt > 0.05f) dt = 0.05f;
            prev = tn;
            if (t >= dur) break;
            if (limit > 0 && tn - start > limit) { run = 0; break; }

            sc->frame(t, dt);
            float fade = fminf(1.0f, fminf(t / FADE, (dur - t) / FADE));
            if (fade < 1.0f) {
                int k = (int)(fade * 256);
                for (int i = 0; i < W * H; i++) buf[i] = scale16(buf[i], k);
            }
            for (int y = 0; y < H; y++) {
                uint32_t *dst = (uint32_t *)(fb + (size_t)y * fi.line_length);
                const uint16_t *src = buf + y * W;
                for (int x = 0; x < W; x++) dst[x] = src[x] | ((uint32_t)src[x] << 16);
            }
            frames++;
            double ft = now() - tn;
            if (ft > worst) worst = ft;

            next += 0.02;
            struct timespec ts = {(time_t)next, (long)((next - (time_t)next) * 1e9)};
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
            if (now() > next + 0.1) next = now();
        }
        if (log && frames) {
            fprintf(log, "%-8s %5.1f fps  worst %5.1f ms\n", sc->name, frames / (now() - s0), worst * 1000);
            fflush(log);
        }
    }

    memset(fb, 0, fbsize);
    ioctl(0, KDSETMODE, KD_TEXT);
    munmap(fb, fbsize);
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// bc101 demo sequencer: plays scenes in a loop on a 720x288 RGB565 fbdev and takes commands from
// the control socket (ctl.h), which is what the remote (remote.c, tools/tv-dispatch) talks to.
//   bc101 [-x] [-s socket] [-t seconds] [scene|channel ...]   run the show (default: the loop)
//   bc101 -l                                                  list the channels
//   bc101 ctl [-s socket] <command> [args]                     one command, prints the reply
//   bc101 remote [-s socket] [--admin] [--name who] [--frame]  the zapper
// (ctl and remote are also the standalone client tvctl, see tvctl.c, which carries no scene code)
// -x renders without a framebuffer (for tests on any Linux box), -s sets the socket path
// (default: $RUNTIME_DIRECTORY/ctl under systemd). Fonts and slides are looked up next to the
// executable (fonts/, slides/).
//
// Commands: status | list | log | ping | next | auto | tune <ch|name> | hold <ch|name> |
//           play <ch|name> [seconds] | reexec
// Modes: auto plays the loop; hold repeats one scene until next/auto; once (play) shows a scene
// one time and returns to the loop. Every change goes through the normal fade.
#include "core.h"
#include "ctl.h"
#include "remote.h"
#include "scenes.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FADE 0.4f
#define FRAME 0.02

typedef enum { M_AUTO, M_HOLD, M_ONCE } Mode;
static const char *const mode_name[] = {"auto", "hold", "once"};

typedef struct {
    int frames;
    double secs, worst;
} Stat;

static struct {
    const Scene *sel[64]; // the loop
    int nsel, li;         // and the next position in it
    Mode mode;
    const Scene *pin;     // target of hold/once
    float once;           // seconds for once, 0 = the scene's own length
    int once_playing;
    const Scene *cur;
    int skip, reexec;
    double s0, start;
    float dur;
    int frames;
    double worst;
    Stat st[CH_MAX + 1]; // per channel, for `log`
} S;

static volatile sig_atomic_t run = 1;
static void on_sig(int s) { (void)s; run = 0; }

static char self_path[512]; // for reexec: the path, not /proc/self/exe, which names the old inode

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static unsigned long self_inode(void) {
    struct stat st;
    return stat("/proc/self/exe", &st) ? 0 : (unsigned long)st.st_ino;
}

static const Scene *pick(void) {
    if (S.mode == M_HOLD) return S.pin;
    if (S.mode == M_ONCE) { S.once_playing = 1; return S.pin; }
    const Scene *sc = S.sel[S.li];
    S.li = (S.li + 1) % S.nsel;
    return sc;
}

static const Scene *upcoming(void) {
    if (S.mode == M_HOLD) return S.pin;
    if (S.mode == M_ONCE && !S.once_playing) return S.pin;
    return S.sel[S.li];
}

// Switch to sc after the fade; a scene already on air just changes mode.
static void retune(const Scene *sc, Mode m, float secs) {
    S.pin = sc;
    S.mode = m;
    S.once = secs;
    S.once_playing = 0;
    if (!(sc == S.cur && m == M_HOLD)) S.skip = 1;
}

static void handle(int cfd, char *req, float t) {
    char out[CTL_MAX_REPLY];
    char *a[4] = {0};
    int n = 0;
    for (char *p = strtok(req, " \t"); p && n < 4; p = strtok(NULL, " \t")) a[n++] = p;
    if (!n) { ctl_reply(cfd, "err empty\n"); return; }

    if (!strcmp(a[0], "ping")) {
        snprintf(out, sizeof out, "ok\n");
    } else if (!strcmp(a[0], "status")) {
        const Scene *nx = upcoming();
        double el = now() - S.s0;
        snprintf(out, sizeof out,
                 "ok\nscene=%s ch=%d t=%.1f dur=%.1f mode=%s next=%s nextch=%d fps=%.1f worst=%.1f ino=%lu up=%.0f\n",
                 S.cur->name, scene_channel(S.cur), t, S.dur, mode_name[S.mode], nx->name, scene_channel(nx),
                 el > 0 ? S.frames / el : 0.0, S.worst * 1000, self_inode(), now() - S.start);
    } else if (!strcmp(a[0], "list")) {
        size_t l = (size_t)snprintf(out, sizeof out, "ok\n");
        scene_list_str(out + l, sizeof out - l);
    } else if (!strcmp(a[0], "log")) {
        size_t l = (size_t)snprintf(out, sizeof out, "ok\n");
        for (int ch = 0; ch <= CH_MAX && l < sizeof out; ch++)
            if (S.st[ch].frames)
                l += (size_t)snprintf(out + l, sizeof out - l, "%2d %-8s %5.1f fps  worst %5.1f ms  %d frames\n", ch,
                                      scene_by_channel(ch)->name, S.st[ch].frames / S.st[ch].secs,
                                      S.st[ch].worst * 1000, S.st[ch].frames);
    } else if (!strcmp(a[0], "next")) {
        S.mode = M_AUTO;
        S.once_playing = 0;
        S.skip = 1;
        snprintf(out, sizeof out, "ok %s\n", upcoming()->name);
    } else if (!strcmp(a[0], "auto")) {
        if (S.mode != M_AUTO) {
            S.mode = M_AUTO;
            S.once_playing = 0;
            S.skip = 1;
        }
        snprintf(out, sizeof out, "ok\n");
    } else if ((!strcmp(a[0], "tune") || !strcmp(a[0], "hold") || !strcmp(a[0], "play")) && n >= 2) {
        const Scene *sc = scene_parse(a[1]);
        if (!sc) {
            snprintf(out, sizeof out, "err no such channel: %s\n", a[1]);
        } else if (!strcmp(a[0], "play")) {
            float secs = n >= 3 ? (float)atof(a[2]) : 0;
            if (secs < 0 || secs > 3600) {
                snprintf(out, sizeof out, "err seconds out of range\n");
            } else {
                retune(sc, M_ONCE, secs);
                snprintf(out, sizeof out, "ok %d %s once\n", scene_channel(sc), sc->name);
            }
        } else {
            retune(sc, M_HOLD, 0);
            snprintf(out, sizeof out, "ok %d %s hold\n", scene_channel(sc), sc->name);
        }
    } else if (!strcmp(a[0], "reexec")) {
        S.reexec = 1;
        S.skip = 1;
        snprintf(out, sizeof out, "ok restarting %s\n", self_path);
    } else {
        snprintf(out, sizeof out, "err unknown command: %s\n", a[0]);
    }
    ctl_reply(cfd, out);
}

// Replace the process by the binary at our path (after an upload it is a new one) and carry the
// loop position and mode over as `-r li:mode:pinchannel`.
static void do_reexec(int argc, char **argv) {
    char *nargv[72];
    char state[48];
    int n = 0;
    for (int i = 0; i < argc && n < 68; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) { i++; continue; }
        nargv[n++] = argv[i];
    }
    snprintf(state, sizeof state, "%d:%d:%d", S.li, (int)S.mode, S.pin ? scene_channel(S.pin) : -1);
    nargv[n++] = "-r";
    nargv[n++] = state;
    nargv[n] = NULL;
    execv(self_path, nargv);
    perror(self_path); // the new binary is unusable: exit non-zero, systemd restarts whatever is on disk
    exit(1);
}

static void resume(const char *s) {
    int li = 0, mode = 0, ch = -1;
    if (sscanf(s, "%d:%d:%d", &li, &mode, &ch) < 2) return;
    if (S.nsel) S.li = ((li % S.nsel) + S.nsel) % S.nsel;
    const Scene *sc = ch >= 0 ? scene_by_channel(ch) : NULL;
    if (sc && (mode == M_HOLD || mode == M_ONCE)) {
        S.pin = sc;
        S.mode = (Mode)mode;
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "ctl")) return ctl_main(argc - 1, argv + 1);
    if (argc > 1 && !strcmp(argv[1], "remote")) return remote_main(argc - 1, argv + 1);

    double limit = 0;
    int headless = 0;
    const char *sock = NULL, *res = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) {
            scene_list();
            return 0;
        } else if (!strcmp(argv[i], "-x")) {
            headless = 1;
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            limit = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            sock = argv[++i];
        } else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            res = argv[++i];
        } else {
            const Scene *sc = scene_parse(argv[i]);
            if (!sc) { fprintf(stderr, "unknown scene: %s\n", argv[i]); return 1; }
            if (S.nsel >= 64) { fprintf(stderr, "too many scenes (max 64)\n"); return 1; }
            S.sel[S.nsel++] = sc;
        }
    }
    if (!S.nsel)
        for (int k = 0; k < scene_nloop; k++) S.sel[S.nsel++] = scene_loop[k];
    if (res) resume(res);

    ssize_t sl = readlink("/proc/self/exe", self_path, sizeof self_path - 1);
    if (sl <= 0 || asset_init()) {
        perror("/proc/self/exe");
        return 1;
    }
    self_path[sl] = 0;
    if (load_font(&fbig, asset("fonts/big.psf")) || load_font(&fsml, asset("fonts/small.psf"))) {
        fprintf(stderr, "font load failed in %s\n", asset("fonts/"));
        return 1;
    }

    uint8_t *fb = NULL;
    size_t fbsize = 0;
    unsigned stride = 0;
    if (!headless) {
        int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if (fd < 0) { perror("/dev/fb0"); return 1; }
        struct fb_var_screeninfo vi;
        struct fb_fix_screeninfo fi;
        ioctl(fd, FBIOGET_VSCREENINFO, &vi);
        ioctl(fd, FBIOGET_FSCREENINFO, &fi);
        if (vi.bits_per_pixel != 16 || vi.xres != 2 * W || vi.yres != H) {
            fprintf(stderr, "unexpected fb: %ux%u %ubpp\n", vi.xres, vi.yres, vi.bits_per_pixel);
            return 1;
        }
        stride = fi.line_length;
        fbsize = (size_t)stride * vi.yres;
        fb = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (fb == MAP_FAILED) { perror("mmap"); return 1; }
        close(fd);
    }

    // Where the remote connects. No socket when not under systemd and none was asked for.
    if (!sock && getenv("RUNTIME_DIRECTORY")) sock = ctl_default_path();
    int lfd = -1;
    if (sock && (lfd = ctl_listen(sock)) < 0) fprintf(stderr, "control socket %s: %s\n", sock, strerror(errno));

    char logpath[1024];
    const char *rt = getenv("RUNTIME_DIRECTORY");
    snprintf(logpath, sizeof logpath, "%s/anim.log", rt && *rt ? rt : "/tmp");
    FILE *log = fopen(logpath, "w");

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);
    if (fb) ioctl(0, KDSETMODE, KD_GRAPHICS);

    S.start = now();
    while (run) {
        const Scene *sc = pick();
        S.cur = sc;
        S.skip = 0;
        if (sc->init) sc->init();
        float dur = sc->length ? sc->length() : sc->dur;
        if (S.mode == M_ONCE && S.once > 0) dur = S.once;
        S.dur = dur;
        double s0 = now(), prev = s0, next = s0, worst = 0;
        int frames = 0;
        S.s0 = s0;
        S.frames = 0;
        S.worst = 0;
        while (run) {
            double tn = now();
            float t = (float)(tn - s0);
            float dt = (float)(tn - prev);
            if (dt > 0.05f) dt = 0.05f;
            prev = tn;
            if (t >= dur) break;
            if (limit > 0 && tn - S.start > limit) { run = 0; break; }

            if (lfd >= 0) {
                char req[CTL_MAX_REQ];
                int cfd;
                for (int k = 0; k < 4 && (cfd = ctl_accept(lfd, req, sizeof req)) >= 0; k++) handle(cfd, req, t);
                if (S.skip) {
                    if (t + FADE < dur) dur = t + FADE;
                    S.dur = dur;
                    S.skip = 0;
                }
            }

            sc->frame(t, dt);
            float fade = fminf(1.0f, fminf(t / FADE, (dur - t) / FADE));
            if (fade < 1.0f) {
                int k = (int)(fade * 256);
                for (int i = 0; i < W * H; i++) buf[i] = scale16(buf[i], k);
            }
            if (fb)
                for (int y = 0; y < H; y++) {
                    uint32_t *dst = (uint32_t *)(fb + (size_t)y * stride);
                    const uint16_t *src = buf + y * W;
                    for (int x = 0; x < W; x++) dst[x] = src[x] | ((uint32_t)src[x] << 16);
                }
            frames++;
            double ft = now() - tn;
            if (ft > worst) worst = ft;
            S.frames = frames;
            S.worst = worst;

            next += FRAME;
            struct timespec ts = {(time_t)next, (long)((next - (time_t)next) * 1e9)};
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
            if (now() > next + 0.1) next = now();
        }
        double secs = now() - s0;
        int ch = scene_channel(sc);
        if (frames && ch >= 0) {
            S.st[ch].frames += frames;
            S.st[ch].secs += secs;
            if (worst > S.st[ch].worst) S.st[ch].worst = worst;
        }
        if (log && frames) {
            fprintf(log, "%-8s %5.1f fps  worst %5.1f ms\n", sc->name, frames / secs, worst * 1000);
            fflush(log);
        }
        if (S.mode == M_ONCE && S.once_playing) {
            S.mode = M_AUTO;
            S.once_playing = 0;
        }
        if (S.reexec && run) {
            if (fb) memset(fb, 0, fbsize);
            if (log) fclose(log);
            do_reexec(argc, argv);
        }
    }

    if (fb) {
        memset(fb, 0, fbsize);
        ioctl(0, KDSETMODE, KD_TEXT);
        munmap(fb, fbsize);
    }
    if (lfd >= 0) {
        close(lfd);
        unlink(sock);
    }
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// The zapper: the show's remote control, drawn in the terminal over the control socket (ctl.h).
// tools/tv-dispatch starts it for a plain `ssh tv`; by hand: bc101 remote -s <socket>.
//   bc101 remote [-s socket] [--admin] [--name who] [--frame] [--ascii] [--no-color]
// Keys: 0-9 channel (two digits, like a TV set), up/down or +/- zap, Enter or Space hold/auto,
//       n next, a auto, i info (fps per channel), u upload hint, x delete a visual (admin),
//       r redraw, ? help, q quit. --frame draws one picture to stdout and exits (tests, screenshots).
// Only ANSI escapes, 16 colours and UTF-8 block glyphs: works in any terminal, also on a phone.
#include "remote.h"
#include "ctl.h"

#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ENTRY_SECS 1.5
#define POLL_SECS 0.5
#define MSG_SECS 4.0
#define CH_LIST_MAX 100

typedef struct {
    int ch;
    char name[20], kind[8];
} Chan;

enum { V_MAIN, V_INFO, V_HELP };

static struct {
    const char *sock, *who;
    int admin, frame_only, ascii, color;
    Chan ch[CH_LIST_MAX];
    int nch;
    // last status
    int ok, cur, nextch;
    char scene[20], mode[8], next[20];
    float t, dur, fps, worst;
    unsigned long ino, up;
    // ui state
    int w, h, view, tick, confirm, dirty;
    char entry[3];
    double entry_t, msg_t;
    char msg[120], info[CTL_MAX_REPLY];
} R;

static volatile sig_atomic_t resized;
static void on_winch(int s) { (void)s; resized = 1; }

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---------- glyphs ----------

// LED-style digits, 4 columns by 3 rows: F full block, T upper half, B lower half.
static const char *const DIG[11][3] = {
    {"FTTF", "F  F", "TTTT"}, {"  TF", "   F", "   T"}, {"TTTF", "FTTT", "TTTT"}, {"TTTF", " TTF", "TTTT"},
    {"F  F", "TTTF", "   T"}, {"FTTT", "TTTF", "TTTT"}, {"FTTT", "FTTF", "TTTT"}, {"TTTF", "   F", "   T"},
    {"FTTF", "FTTF", "TTTT"}, {"FTTF", "TTTF", "TTTT"}, {"    ", "BBBB", "    "}, // 10: the empty digit
};

static const char *gl(char c) {
    switch (c) {
    case 'F': return R.ascii ? "#" : "\xe2\x96\x88";  // █
    case 'T': return R.ascii ? "^" : "\xe2\x96\x80";  // ▀
    case 'B': return R.ascii ? "_" : "\xe2\x96\x84";  // ▄
    case 's': return R.ascii ? "-" : "\xe2\x96\x91";  // ░
    case '>': return R.ascii ? ">" : "\xe2\x96\xb8";  // ▸
    case 'o': return R.ascii ? "*" : "\xe2\x97\x8f";  // ●
    case '.': return R.ascii ? "o" : "\xe2\x97\x8b";  // ○
    case '-': return R.ascii ? "-" : "\xe2\x94\x80";  // ─
    case 'u': return R.ascii ? "^" : "\xe2\x86\x91";  // ↑
    case 'd': return R.ascii ? "v" : "\xe2\x86\x93";  // ↓
    case '*': return R.ascii ? "*" : "\xc2\xb7";      // ·
    default: return " ";
    }
}

// ---------- output builder: tracks the visible column so padding is exact ----------

typedef struct {
    char *s;
    size_t n, len;
    int col;
} Out;

static void o_raw(Out *o, const char *s) {
    size_t l = strlen(s);
    if (o->len + l < o->n) {
        memcpy(o->s + o->len, s, l);
        o->len += l;
        o->s[o->len] = 0;
    }
}

static int cps(const char *s) {
    int c = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) c++;
    return c;
}

static void o_txt(Out *o, const char *s) {
    o_raw(o, s);
    o->col += cps(s);
}

static void o_fmt(Out *o, const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    o_txt(o, b);
}

static void o_sgr(Out *o, const char *code) {
    if (!R.color) return;
    o_raw(o, "\x1b[");
    o_raw(o, code);
    o_raw(o, "m");
}

static void o_pad(Out *o, int to) {
    while (o->col < to) o_txt(o, " ");
}

static void o_glyphs(Out *o, const char *codes) {
    for (; *codes; codes++) o_txt(o, gl(*codes));
}

static void o_nl(Out *o) {
    o_sgr(o, "0");
    o_raw(o, R.color ? "\x1b[K\r\n" : "\r\n");
    o->col = 0;
}

// ---------- talking to the show ----------

static void copy(char *dst, const char *src, size_t n) {
    snprintf(dst, n, "%s", src);
}

static int fetch_status(void) {
    char rep[CTL_MAX_REPLY];
    if (ctl_request(R.sock, "status", rep, sizeof rep) != 0) {
        R.ok = 0;
        return -1;
    }
    char *line = strchr(rep, '\n');
    if (!line) { R.ok = 0; return -1; }
    for (char *tok = strtok(line + 1, " \n"); tok; tok = strtok(NULL, " \n")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(tok, "scene")) copy(R.scene, v, sizeof R.scene);
        else if (!strcmp(tok, "ch")) R.cur = atoi(v);
        else if (!strcmp(tok, "t")) R.t = (float)atof(v);
        else if (!strcmp(tok, "dur")) R.dur = (float)atof(v);
        else if (!strcmp(tok, "mode")) copy(R.mode, v, sizeof R.mode);
        else if (!strcmp(tok, "next")) copy(R.next, v, sizeof R.next);
        else if (!strcmp(tok, "nextch")) R.nextch = atoi(v);
        else if (!strcmp(tok, "fps")) R.fps = (float)atof(v);
        else if (!strcmp(tok, "worst")) R.worst = (float)atof(v);
        else if (!strcmp(tok, "ino")) R.ino = strtoul(v, NULL, 10);
        else if (!strcmp(tok, "up")) R.up = strtoul(v, NULL, 10);
    }
    R.ok = 1;
    return 0;
}

static int fetch_list(void) {
    char rep[CTL_MAX_REPLY];
    if (ctl_request(R.sock, "list", rep, sizeof rep) != 0) return -1;
    R.nch = 0;
    for (char *line = strchr(rep, '\n'); line && *line; line = strchr(line, '\n')) {
        line++;
        Chan *c = &R.ch[R.nch];
        if (R.nch < CH_LIST_MAX && sscanf(line, "%d %19s %*s %7s", &c->ch, c->name, c->kind) == 3) R.nch++;
    }
    return 0;
}

static void fetch_info(void) {
    if (ctl_request(R.sock, "log", R.info, sizeof R.info) != 0) copy(R.info, "ok\n(keine Daten)\n", sizeof R.info);
}

static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(R.msg, sizeof R.msg, fmt, ap);
    va_end(ap);
    R.msg_t = now() + MSG_SECS;
    R.dirty = 1;
}

static const Chan *chan(int ch) {
    for (int i = 0; i < R.nch; i++)
        if (R.ch[i].ch == ch) return &R.ch[i];
    return NULL;
}

// One command to the show; the message line shows the outcome.
static void command(const char *fmt, ...) {
    char req[CTL_MAX_REQ], rep[CTL_MAX_REPLY];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req, sizeof req, fmt, ap);
    va_end(ap);
    int rc = ctl_request(R.sock, req, rep, sizeof rep);
    rep[strcspn(rep, "\n")] = 0;
    if (rc < 0) say("keine Verbindung zur Show");
    else if (rc) say("%s", rep + (strncmp(rep, "err ", 4) ? 0 : 4));
    else if (strlen(rep) > 3) say("%s %s", gl('>'), rep + 3);
    fetch_status();
    R.dirty = 1;
}

static void tune(int ch) {
    const Chan *c = chan(ch);
    if (!c) {
        say("kein Programm %02d", ch);
        return;
    }
    command("tune %d", ch);
    say("%s P%02d %s", gl('>'), ch, c->name);
}

static void zap(int dir) {
    if (!R.nch) return;
    int i = 0;
    while (i < R.nch && R.ch[i].ch != R.cur) i++;
    if (i == R.nch) i = 0;
    i = (i + dir + R.nch) % R.nch;
    tune(R.ch[i].ch);
}

static void commit_entry(void) {
    if (!R.entry[0]) return;
    int ch = atoi(R.entry);
    R.entry[0] = 0;
    tune(ch);
}

// ---------- drawing ----------

static void term_size(void) {
    struct winsize ws;
    R.w = 80;
    R.h = 24;
    if (!ioctl(1, TIOCGWINSZ, &ws) && ws.ws_col > 0 && ws.ws_row > 0) {
        R.w = ws.ws_col;
        R.h = ws.ws_row;
    } else {
        const char *c = getenv("COLUMNS"), *l = getenv("LINES");
        if (c && atoi(c) > 0) R.w = atoi(c);
        if (l && atoi(l) > 0) R.h = atoi(l);
    }
    if (R.w > 200) R.w = 200;
}

static const char *kind_color(const char *kind) {
    if (!strcmp(kind, "extra")) return "36";
    if (!strcmp(kind, "visual")) return "35";
    if (!strcmp(kind, "standby")) return "90";
    return "37";
}

static void draw_header(Out *o) {
    char clock[16];
    time_t tt = time(NULL);
    strftime(clock, sizeof clock, "%H:%M:%S", localtime(&tt));
    o_sgr(o, "44;97;1");
    o_fmt(o, "  tvtv %s ZAPPER", gl('>'));
    o_sgr(o, "22");
    if (R.who) o_fmt(o, "  %s %s%s", gl('*'), R.who, R.admin ? " (admin)" : "");
    int right = R.w - 1 - 8 - 2 - 1;
    if (R.entry[0]) {
        o_pad(o, right - 6);
        o_sgr(o, "93;1");
        o_fmt(o, "P%s%s", R.entry, R.entry[1] ? "" : "_");
        o_sgr(o, "22;97");
    }
    o_pad(o, right);
    o_txt(o, gl(R.ok ? (R.tick & 1 ? 'o' : '.') : '.'));
    o_fmt(o, " %s ", clock);
    o_pad(o, R.w);
    o_nl(o);
}

static void draw_digits_row(Out *o, int row) {
    int tens = 10, ones = 10;
    if (R.entry[0]) {
        tens = R.entry[0] - '0';
        if (R.entry[1]) ones = R.entry[1] - '0';
    } else if (R.ok) {
        tens = R.cur / 10;
        ones = R.cur % 10;
    }
    o_txt(o, "  ");
    o_sgr(o, R.entry[0] ? "92;1" : "93;1");
    o_glyphs(o, DIG[tens][row]);
    o_txt(o, " ");
    o_glyphs(o, DIG[ones][row]);
    o_sgr(o, "0");
    o_txt(o, "   ");
}

static void draw_status(Out *o) {
    // row 1: name, kind, mode badge, what comes next
    draw_digits_row(o, 0);
    if (!R.ok) {
        o_sgr(o, "91;1");
        o_txt(o, "keine Verbindung zur Show");
        o_nl(o);
        draw_digits_row(o, 1);
        o_sgr(o, "90");
        o_fmt(o, "socket %s", R.sock);
        o_nl(o);
        draw_digits_row(o, 2);
        o_nl(o);
        return;
    }
    const Chan *c = chan(R.cur);
    char up[32];
    o_sgr(o, "1;97");
    for (const char *p = R.scene; *p; p++) o_fmt(o, "%c", *p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    o_sgr(o, "0;90");
    o_fmt(o, "  %s", c ? c->kind : "");
    o_pad(o, 40);
    if (!strcmp(R.mode, "hold")) {
        o_sgr(o, "41;97;1");
        o_txt(o, " HOLD ");
        o_sgr(o, "0;90");
        o_txt(o, "  bis n oder Enter");
    } else {
        o_sgr(o, !strcmp(R.mode, "once") ? "43;30;1" : "42;30;1");
        o_txt(o, !strcmp(R.mode, "once") ? " ONCE " : " AUTO ");
        o_sgr(o, "0;90");
        o_fmt(o, "  %s %02d %s", R.ascii ? "next" : "nächstes", R.nextch, R.next);
    }
    o_nl(o);
    // row 2: progress
    draw_digits_row(o, 1);
    int bw = R.w - 40;
    if (bw > 36) bw = 36;
    if (bw < 10) bw = 10;
    float frac = R.dur > 0 ? R.t / R.dur : 0;
    if (frac > 1) frac = 1;
    int filled = (int)(frac * bw + 0.5f);
    static const char *const rainbow[] = {"91", "93", "92", "96", "94", "95"};
    for (int i = 0; i < bw; i++) {
        if (i < filled) {
            o_sgr(o, rainbow[(i / 3) % 6]);
            o_txt(o, gl('F'));
        } else {
            o_sgr(o, "90");
            o_txt(o, gl('s'));
        }
    }
    o_sgr(o, "0;37");
    o_fmt(o, "  %4.1f s / %.0f s", R.t, R.dur);
    o_nl(o);
    // row 3: numbers
    draw_digits_row(o, 2);
    if (R.up >= 3600) snprintf(up, sizeof up, "%luh%02lum", R.up / 3600, (R.up / 60) % 60);
    else snprintf(up, sizeof up, "%lum%02lus", R.up / 60, R.up % 60);
    o_sgr(o, "37");
    o_fmt(o, "%4.1f fps %s worst %.1f ms %s on air %s", R.fps, gl('*'), R.worst, gl('*'), up);
    o_nl(o);
}

static void draw_grid(Out *o, int rows_avail) {
    int namew = 7;
    for (int i = 0; i < R.nch; i++)
        if ((int)strlen(R.ch[i].name) > namew) namew = (int)strlen(R.ch[i].name);
    if (namew > 16) namew = 16;
    int cellw = 4 + namew + 2, cols = (R.w - 2) / cellw;
    if (cols < 1) cols = 1;
    int rows = R.nch ? (R.nch + cols - 1) / cols : 1;
    if (rows > rows_avail) rows = rows_avail;
    o_sgr(o, "90");
    o_fmt(o, "  PROGRAMME %s %d", gl('*'), R.nch);
    o_nl(o);
    for (int r = 0; r < rows; r++) {
        o_txt(o, " ");
        for (int col = 0; col < cols; col++) {
            int i = col * rows + r;
            if (i >= R.nch) break;
            const Chan *c = &R.ch[i];
            int cur = R.ok && c->ch == R.cur;
            if (cur) {
                o_sgr(o, "7;1");
                o_txt(o, gl('>'));
            } else {
                o_sgr(o, kind_color(c->kind));
                o_txt(o, " ");
            }
            o_fmt(o, "%02d %-*.*s", c->ch, namew, namew, c->name);
            o_sgr(o, "0");
            o_txt(o, "  ");
        }
        o_nl(o);
    }
    for (int r = rows; r < rows_avail; r++) o_nl(o);
}

static void draw_info(Out *o, int rows_avail) {
    o_sgr(o, "90");
    o_fmt(o, "  INFO %s Bilder pro Sekunde je Programm (i: zurück)", gl('*'));
    o_nl(o);
    int r = 1;
    for (char *line = strchr(R.info, '\n'); line && *line && r < rows_avail; line = strchr(line, '\n')) {
        line++;
        char l[160];
        size_t n = strcspn(line, "\n");
        if (n >= sizeof l) n = sizeof l - 1;
        memcpy(l, line, n);
        l[n] = 0;
        if (!*l) continue;
        o_sgr(o, "37");
        o_fmt(o, "  %s", l);
        o_nl(o);
        r++;
    }
    for (; r < rows_avail; r++) o_nl(o);
}

static void draw_help(Out *o, int rows_avail) {
    const char *lines[] = {
        "  TASTEN",
        "  0-9        Programm wählen, zweistellig wie am Fernseher",
        "  %s %s  + -    zappen",
        "  Enter      Programm halten, noch einmal: zurück zur Automatik",
        "  n          weiter zum nächsten Programm",
        "  a          Automatik (die Schleife)",
        "  i          Info: fps je Programm",
        "  u          so kommt ein neues Visual auf den Pi",
        "  x          Visual löschen (nur Admin-Keys)",
        "  r          neu zeichnen       q  Ende",
        "",
        "  Programm 00 ist Standby: Bild aus, Show bleibt an.",
    };
    int n = (int)(sizeof lines / sizeof lines[0]), r = 0;
    for (; r < n && r < rows_avail; r++) {
        o_sgr(o, r == 0 ? "90" : "37");
        if (strstr(lines[r], "%s")) o_fmt(o, lines[r], gl('u'), gl('d'));
        else o_txt(o, lines[r]);
        o_nl(o);
    }
    for (; r < rows_avail; r++) o_nl(o);
}

static void draw_footer(Out *o) {
    // message line
    o_txt(o, "  ");
    if (R.confirm) {
        o_sgr(o, "41;97;1");
        o_fmt(o, " Visual %s wirklich löschen?  y / n ", R.scene);
    } else if (R.msg[0]) {
        o_sgr(o, "1;97");
        o_txt(o, R.msg);
    }
    o_nl(o);
    // key legend
    o_sgr(o, "44;97");
    o_fmt(o, " 0-9 Programm  %s%s zappen  Enter halten/auto  n weiter  i info  ? hilfe  q ende", gl('u'), gl('d'));
    o_pad(o, R.w);
    o_nl(o);
}

static void render(Out *o) {
    o->len = 0;
    o->col = 0;
    if (R.color) o_raw(o, "\x1b[H");
    if (R.w < 50 || R.h < 12) {
        o_fmt(o, "Terminal zu klein (%dx%d), mindestens 50x12", R.w, R.h);
        o_nl(o);
        return;
    }
    draw_header(o);
    o_nl(o);
    draw_status(o);
    o_nl(o);
    int rows_avail = R.h - 9; // header, blank, 3 status, blank, grid title, message, legend
    if (R.view == V_INFO) draw_info(o, rows_avail);
    else if (R.view == V_HELP) draw_help(o, rows_avail);
    else draw_grid(o, rows_avail - 1);
    draw_footer(o);
    if (o->len && o->s[o->len - 1] == '\n') o->len -= 2; // no scroll on the last line
    o->s[o->len] = 0;
}

// ---------- terminal ----------

static struct termios saved;
static int rawmode;

static void raw_only(void) {
    struct termios t = saved;
    cfmakeraw(&t);
    t.c_oflag |= OPOST | ONLCR;
    tcsetattr(0, TCSANOW, &t);
}

static void raw_on(void) {
    tcgetattr(0, &saved);
    raw_only();
    rawmode = 1;
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    fflush(stdout);
}

static void raw_off(void) {
    if (!rawmode) return;
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    tcsetattr(0, TCSANOW, &saved);
    rawmode = 0;
}

enum { K_NONE = -1, K_UP = 1000, K_DOWN, K_RIGHT, K_LEFT, K_ENTER, K_ESC };

static int read_key(int timeout_ms) {
    struct pollfd p = {0, POLLIN, 0};
    if (poll(&p, 1, timeout_ms) <= 0) return K_NONE;
    unsigned char c;
    if (read(0, &c, 1) != 1) return 'q'; // hung up
    if (c == '\r' || c == '\n') return K_ENTER;
    if (c == 3 || c == 4) return 'q';
    if (c != 27) return c;
    struct pollfd p2 = {0, POLLIN, 0};
    if (poll(&p2, 1, 60) <= 0) return K_ESC;
    unsigned char b[8];
    ssize_t n = read(0, b, sizeof b);
    if (n >= 2 && (b[0] == '[' || b[0] == 'O')) {
        switch (b[1]) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        }
    }
    return K_NONE;
}

// Deleting goes through the dispatcher, which checks the key's role again and rebuilds the show.
static void delete_visual(const char *name) {
    const char *d = getenv("TVTV_DISPATCH");
    if (!d || !*d) {
        say("löschen geht hier nur so: ssh tv rm %s", name);
        return;
    }
    raw_off();
    printf("\x1b[2J\x1b[H$ tv rm %s\n", name);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        execl(d, d, "rm", name, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    if (pid > 0) waitpid(pid, &st, 0);
    printf("\n[rc %d]  weiter mit einer Taste ", WIFEXITED(st) ? WEXITSTATUS(st) : 1);
    fflush(stdout);
    raw_only();
    read_key(60000);
    raw_on();
    fetch_list();
    fetch_status();
    R.dirty = 1;
}

static void handle_key(int k, double tnow) {
    if (R.confirm) {
        char name[20];
        copy(name, R.scene, sizeof name);
        R.confirm = 0;
        if (k == 'y' || k == 'Y') delete_visual(name);
        else say("nicht gelöscht");
        return;
    }
    if (k >= '0' && k <= '9') {
        size_t l = strlen(R.entry);
        if (l < 2) {
            R.entry[l] = (char)k;
            R.entry[l + 1] = 0;
        }
        R.entry_t = tnow;
        if (l + 1 == 2) commit_entry();
        R.dirty = 1;
        return;
    }
    switch (k) {
    case K_UP: case '+': case '=': case 'k': case K_RIGHT: zap(+1); break;
    case K_DOWN: case '-': case 'j': case K_LEFT: zap(-1); break;
    case K_ENTER: case ' ':
        if (!strcmp(R.mode, "hold")) command("auto");
        else if (R.ok) command("tune %d", R.cur);
        break;
    case 'n': command("next"); break;
    case 'a': command("auto"); break;
    case 'i':
        R.view = R.view == V_INFO ? V_MAIN : V_INFO;
        if (R.view == V_INFO) fetch_info();
        break;
    case '?': case 'h': R.view = R.view == V_HELP ? V_MAIN : V_HELP; break;
    case 'u':
        say(R.admin ? "vom Rechner: ssh tv upload <name> < <name>.c" : "Upload nur mit Admin-Key");
        break;
    case 'x': {
        const Chan *c = chan(R.cur);
        if (!R.admin) say("löschen nur mit Admin-Key");
        else if (!c || strcmp(c->kind, "visual")) say("nur Visuals (30+) lassen sich löschen");
        else R.confirm = 1;
        break;
    }
    case 'r': resized = 1; break;
    case K_ESC:
        if (R.view != V_MAIN) R.view = V_MAIN;
        else R.entry[0] = 0;
        break;
    default: break;
    }
    R.dirty = 1;
}

static int usage(void) {
    fprintf(stderr, "usage: bc101 remote [-s socket] [--admin] [--name who] [--frame] [--ascii] [--no-color]\n");
    return 2;
}

int remote_main(int argc, char **argv) {
    R.sock = ctl_default_path();
    R.color = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) R.sock = argv[++i];
        else if (!strcmp(argv[i], "--admin")) R.admin = 1;
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) R.who = argv[++i];
        else if (!strcmp(argv[i], "--frame")) R.frame_only = 1;
        else if (!strcmp(argv[i], "--ascii")) R.ascii = 1;
        else if (!strcmp(argv[i], "--no-color")) R.color = 0;
        else return usage();
    }
    const char *term = getenv("TERM");
    if (term && !strcmp(term, "dumb")) R.color = 0;

    static char outbuf[65536];
    Out o = {outbuf, sizeof outbuf, 0, 0};
    term_size();
    fetch_list();
    fetch_status();
    if (R.frame_only) {
        render(&o);
        fwrite(o.s, 1, o.len, stdout);
        fputs("\n", stdout);
        return R.ok ? 0 : 1;
    }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "the zapper needs a terminal (ssh -t); for one command use: bc101 ctl <command>\n");
        return 2;
    }

    signal(SIGWINCH, on_winch);
    raw_on();
    atexit(raw_off);
    double last_poll = 0, last_info = 0;
    R.dirty = 1;
    for (;;) {
        double tnow = now();
        if (resized) {
            resized = 0;
            term_size();
            fputs("\x1b[2J", stdout);
            R.dirty = 1;
        }
        if (tnow - last_poll >= POLL_SECS) {
            unsigned long ino = R.ino;
            fetch_status();
            if (R.ok && (ino != R.ino || !R.nch)) fetch_list();
            last_poll = tnow;
            R.tick++;
            R.dirty = 1;
        }
        if (R.view == V_INFO && tnow - last_info >= 2.0) {
            fetch_info();
            last_info = tnow;
            R.dirty = 1;
        }
        if (R.entry[0] && tnow - R.entry_t >= ENTRY_SECS) commit_entry();
        if (R.msg[0] && tnow >= R.msg_t) {
            R.msg[0] = 0;
            R.dirty = 1;
        }
        if (R.dirty) {
            render(&o);
            fwrite(o.s, 1, o.len, stdout);
            fflush(stdout);
            R.dirty = 0;
        }
        int k = read_key(100);
        if (k == 'q' || k == 'Q') break;
        if (k != K_NONE) handle_key(k, now());
    }
    raw_off();
    return 0;
}

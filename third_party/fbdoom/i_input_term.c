// SPDX-License-Identifier: GPL-2.0-or-later
//
// Terminal input (tvtv): FBDOOM_INPUT names the terminal to read, e.g. /dev/tty of the SSH
// session that started Doom, or a FIFO that carries what a terminal types.
//
// A terminal that speaks the kitty keyboard protocol reports presses and releases, including
// Ctrl, Shift and Alt on their own; Doom then plays as with a real keyboard. Other terminals
// send only characters and repeats, so a movement key counts as held until it stops repeating.
// The terminal is put back the way it was on every exit and on SIGINT, SIGTERM and SIGHUP; a
// crash (SIGSEGV, SIGKILL) leaves it as Doom set it, `reset` fixes that.

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "d_event.h"
#include "doomkeys.h"
#include "i_system.h"
#include "i_timer.h"

#define HOLD_FIRST_MS  550 // bridges the terminal's delay before it starts repeating
#define HOLD_REPEAT_MS 120
#define ESC_WAIT_MS    40  // a lone ESC is the Esc key, not the start of a sequence

// kitty keyboard protocol: flags 1 (disambiguate) + 2 (event types) + 8 (all keys as codes)
#define TERM_ENTER "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H"
#define TERM_KEYS  "\x1b[>11u\x1b[?u"
#define TERM_LEAVE "\x1b[<u\x1b[?25h\x1b[?1049l"

enum { EV_TAP, EV_PRESS, EV_REPEAT, EV_RELEASE };

static int term_fd = -1;
static struct termios saved;
static volatile sig_atomic_t restored = 1;

static int kitty;            // the terminal reports releases
static int held[256];        // kitty: keys that are down
static int until[256];       // otherwise: release time per key, 0 = up
static int run_on;           // 'r' toggles running
static unsigned char seq[64];
static int seqlen, seq_at, seq_skip;

static void put(const char *s)
{
    size_t n = strlen(s);

    while (n > 0) {
        ssize_t w = write(term_fd, s, n);
        if (w <= 0)
            return;
        s += w;
        n -= (size_t) w;
    }
}

static void term_restore(void)
{
    if (restored)
        return;
    restored = 1;
    put(TERM_LEAVE);
    tcsetattr(term_fd, TCSANOW, &saved);
}

// The terminal first (write and tcsetattr are async-signal-safe), then the signal's own end:
// Doom may be anywhere, also in a loop that never looks at a flag.
static void on_signal(int sig)
{
    term_restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

// ch is the typed character for cheats and savegame names, also when the key moves the player.
static void post(int type, int key, int ch)
{
    event_t ev = {0};

    ev.type = type;
    ev.data1 = key;
    ev.data2 = type == ev_keydown ? ch : 0;
    D_PostEvent(&ev);
}

// Keys that walk, turn, strafe or shoot are held; anything else is a press and a release.
static int movement(int key)
{
    return key == KEY_UPARROW || key == KEY_DOWNARROW || key == KEY_LEFTARROW
        || key == KEY_RIGHTARROW || key == KEY_STRAFE_L || key == KEY_STRAFE_R
        || key == KEY_FIRE || key == KEY_USE;
}

static void key_event(int key, int ch, int event, int now)
{
    if (key <= 0 || key > 255)
        return;
    switch (event) {
    case EV_PRESS:
        if (!held[key]) {
            held[key] = 1;
            post(ev_keydown, key, ch);
        }
        break;
    case EV_RELEASE:
        if (held[key]) {
            held[key] = 0;
            post(ev_keyup, key, 0);
        }
        break;
    case EV_REPEAT:
        break;
    default:
        // every press is a keydown, so a double tap moves twice in the menus
        post(ev_keydown, key, ch);
        if (!movement(key))
            post(ev_keyup, key, 0);
        else
            until[key] = now + (until[key] ? HOLD_REPEAT_MS : HOLD_FIRST_MS);
    }
    if (key == 'r' && (event == EV_TAP || event == EV_PRESS)) {
        // a terminal without releases cannot hold Shift: run is a toggle. After the 'r' itself,
        // which still types: a cheat (idbeholdr) resets on any other keydown in between
        run_on = !run_on;
        post(run_on ? ev_keydown : ev_keyup, KEY_RSHIFT, 0);
    }
}

// A character, or a kitty key code (unicode for text keys), as a Doom key.
static int doom_key(int c)
{
    switch (c) {
    case 'w': return KEY_UPARROW;
    case 's': return KEY_DOWNARROW;
    case 'a': return KEY_STRAFE_L;
    case 'd': return KEY_STRAFE_R;
    case ' ': case 'f': return KEY_FIRE;
    case 'e': return KEY_USE;
    case 27: return KEY_ESCAPE;
    case '\r': case '\n': return KEY_ENTER;
    case '\t': return KEY_TAB;
    case 0x7f: case 0x08: return KEY_BACKSPACE;
    case 57441: case 57447: return KEY_RSHIFT; // Shift: run
    case 57442: case 57448: return KEY_FIRE;   // Ctrl: fire
    case 57443: case 57449: return KEY_RALT;   // Alt: strafe while turning
    }
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    if (c >= 0x21 && c < 0x7f)
        return c;
    return 0;
}

// What the key types: lower case ASCII, space, or nothing.
static int typed(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c >= 0x20 && c < 0x7f ? c : 0;
}

static void quit(void)
{
    I_Quit();
    exit(0);
}

static int f_key(int n) // the number in CSI n ~ (VT220, PuTTY, kitty)
{
    switch (n) {
    case 11: return KEY_F1;
    case 12: return KEY_F2;
    case 13: return KEY_F3;
    case 14: return KEY_F4;
    case 15: return KEY_F5;
    case 17: return KEY_F6;
    case 18: return KEY_F7;
    case 19: return KEY_F8;
    case 20: return KEY_F9;
    case 21: return KEY_F10;
    case 23: return KEY_F11;
    case 24: return KEY_F12;
    }
    return 0;
}

// One escape sequence from seq[0..seqlen): CSI (ESC [) or SS3 (ESC O).
static void sequence(int now)
{
    int val[3][3] = {{0}}, has[3][3] = {{0}}; // [field][subfield]: CSI key:.. ; mods:event ; text
    int f = 0, s = 0, key = 0, ch = 0, code, mods, event;
    unsigned char final = seq[seqlen - 1];
    int i;

    if (seq[1] == 'O') { // SS3: application cursor keys and F1-F4
        switch (final) {
        case 'A': key_event(KEY_UPARROW, 0, EV_TAP, now); break;
        case 'B': key_event(KEY_DOWNARROW, 0, EV_TAP, now); break;
        case 'C': key_event(KEY_RIGHTARROW, 0, EV_TAP, now); break;
        case 'D': key_event(KEY_LEFTARROW, 0, EV_TAP, now); break;
        case 'P': case 'Q': case 'R': case 'S':
            key_event(KEY_F1 + final - 'P', 0, EV_TAP, now);
            break;
        }
        return;
    }
    if (seq[2] == '?') { // answer to our query: CSI ? flags u, flag 2 = releases are reported
        if (final == 'u' && atoi((const char *) seq + 3) & 2)
            kitty = 1;
        return; // also the device attributes answer (CSI ? .. c) of terminals without kitty
    }
    for (i = 2; i < seqlen - 1; i++) {
        unsigned char c = seq[i];

        if (c >= '0' && c <= '9' && f < 3 && s < 3) {
            if (val[f][s] < 1000000)
                val[f][s] = val[f][s] * 10 + c - '0';
            has[f][s] = 1;
        } else if (c == ';') {
            f++;
            s = 0;
        } else if (c == ':') {
            s++;
        }
    }
    code = val[0][0];
    mods = has[1][0] ? val[1][0] : 1;
    if (has[1][1] && val[1][1] >= EV_PRESS && val[1][1] <= EV_RELEASE) {
        event = val[1][1];
        kitty = 1;
    } else {
        event = kitty ? EV_PRESS : EV_TAP;
    }

    switch (final) {
    case 'u':
        if (code == 'c' && (mods - 1) & 4 && event != EV_RELEASE) // Ctrl-C
            quit();
        key = doom_key(code);
        ch = typed(code);
        break;
    case 'A': key = KEY_UPARROW; break;
    case 'B': key = KEY_DOWNARROW; break;
    case 'C': key = KEY_RIGHTARROW; break;
    case 'D': key = KEY_LEFTARROW; break;
    case 'P': case 'Q': case 'R': case 'S': key = KEY_F1 + final - 'P'; break;
    case '~': key = f_key(code); break;
    }
    key_event(key, ch, event, now);
}

void term_events(void)
{
    unsigned char b[128];
    int now, n, i, k;

    if (term_fd < 0)
        return;
    now = I_GetTimeMS();
    while ((n = read(term_fd, b, sizeof b)) > 0) {
        for (i = 0; i < n; i++) {
            unsigned char c = b[i];

            if (seq_skip) { // the rest of a sequence too long to be a key
                if (c >= 0x40 && c <= 0x7e)
                    seq_skip = 0;
            } else if (seqlen > 0) {
                seq[seqlen++] = c;
                if (seqlen == 2 && c != '[' && c != 'O') { // Esc, then another key
                    seqlen = 0;
                    key_event(KEY_ESCAPE, 0, EV_TAP, now);
                    i--;
                } else if (seqlen > 2 && c >= 0x40 && c <= 0x7e) {
                    sequence(now);
                    seqlen = 0;
                } else if (seqlen == (int) sizeof seq) {
                    seqlen = 0;
                    seq_skip = 1;
                }
            } else if (c == 0x1b) {
                seq[0] = c;
                seqlen = 1;
                seq_at = now;
            } else if (c == 0x03) {
                quit();
            } else {
                key_event(doom_key(c), typed(c), EV_TAP, now);
            }
        }
    }
    if (seqlen == 1 && now - seq_at >= ESC_WAIT_MS) {
        seqlen = 0;
        key_event(KEY_ESCAPE, 0, EV_TAP, now);
    }
    for (k = 0; k < 256; k++) {
        if (until[k] && now - until[k] >= 0) {
            until[k] = 0;
            post(ev_keyup, k, 0);
        }
    }
}

static const char help[] =
    "Doom is on the TV. This terminal is the controller.\r\n"
    "\r\n"
    "  arrows, w s      walk and turn       a d        strafe\r\n"
    "  space, f, Ctrl   fire                e          use: doors, switches\r\n"
    "  Shift (or r)     run                 Alt        strafe with the arrows\r\n"
    "  1-7              weapons             Tab        map\r\n"
    "  Esc              menu: new game, episode, skill, quit\r\n"
    "  Ctrl-C           quit, the show comes back\r\n"
    "\r\n";

void term_init(void)
{
    const char *path = getenv("FBDOOM_INPUT");
    struct termios t;
    sigset_t block, old;

    if (!path || !*path)
        return;
    term_fd = open(path, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (term_fd < 0) {
        perror(path);
        return;
    }
    // no signal halfway through: the handler would restore, then the rest switch it on again
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGHUP);
    sigprocmask(SIG_BLOCK, &block, &old);
    if (isatty(term_fd) && tcgetattr(term_fd, &saved) == 0) {
        t = saved;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(ICRNL | INLCR | IXON | BRKINT | ISTRIP);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(term_fd, TCSANOW, &t);
        restored = 0;
        atexit(term_restore);
        put(TERM_ENTER);
        put(help);
        put(TERM_KEYS);
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    sigprocmask(SIG_SETMASK, &old, NULL);
    printf("I_InitInput: terminal input from %s\n", path);
}

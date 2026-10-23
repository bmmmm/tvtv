// SPDX-License-Identifier: GPL-2.0-or-later
//
// Terminal input (tvtv): FBDOOM_INPUT names the terminal to read, e.g. /dev/tty of the SSH
// session that started Doom, or a FIFO that carries what a terminal types.
//
// A terminal that speaks the kitty keyboard protocol reports presses and releases, including
// Ctrl, Shift and Alt on their own; Doom then plays as with a real keyboard. Other terminals
// send only characters and repeats, so a movement key counts as held until it stops repeating.
// The terminal is put back the way it was on every way out: quit, Ctrl-C, SIGTERM, SIGHUP.

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
static int is_tty;
static struct termios saved;
static int restored = 1;
static volatile sig_atomic_t quit_signal;

static int kitty;            // the terminal reports releases
static int held[256];        // kitty: keys that are down
static int until[256];       // otherwise: release time per key, 0 = up
static int run_on;           // 'r' toggles running
static unsigned char seq[64];
static int seqlen, seq_at;

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

// The terminal first (write and tcsetattr are async-signal-safe): a SIGKILL may follow before
// the game loop gets to quit.
static void on_signal(int sig)
{
    term_restore();
    quit_signal = sig;
}

static void post(int type, int key)
{
    event_t ev = {0};

    ev.type = type;
    ev.data1 = key;
    ev.data2 = type == ev_keydown && key < 0x80 ? key : 0;
    D_PostEvent(&ev);
}

// Keys that walk, turn, strafe or shoot are held; anything else is a press and a release.
static int movement(int key)
{
    return key == KEY_UPARROW || key == KEY_DOWNARROW || key == KEY_LEFTARROW
        || key == KEY_RIGHTARROW || key == KEY_STRAFE_L || key == KEY_STRAFE_R
        || key == KEY_FIRE || key == KEY_USE;
}

static void key_event(int key, int event, int now)
{
    if (key <= 0 || key > 255)
        return;
    if (key == 'r') { // a terminal without releases cannot hold Shift: run is a toggle
        if (event == EV_TAP || event == EV_PRESS) {
            run_on = !run_on;
            post(run_on ? ev_keydown : ev_keyup, KEY_RSHIFT);
        }
        return;
    }
    switch (event) {
    case EV_PRESS:
        if (!held[key]) {
            held[key] = 1;
            post(ev_keydown, key);
        }
        break;
    case EV_RELEASE:
        if (held[key]) {
            held[key] = 0;
            post(ev_keyup, key);
        }
        break;
    case EV_REPEAT:
        break;
    default:
        if (!movement(key)) {
            post(ev_keydown, key);
            post(ev_keyup, key);
        } else if (!until[key]) {
            post(ev_keydown, key);
            until[key] = now + HOLD_FIRST_MS;
        } else {
            until[key] = now + HOLD_REPEAT_MS;
        }
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

static void quit(void)
{
    I_Quit();
    exit(0);
}

// One escape sequence from seq[0..seqlen): CSI (ESC [) or SS3 (ESC O).
static void sequence(int now)
{
    int val[3][3] = {{0}}, has[3][3] = {{0}}; // [field][subfield]: CSI key:.. ; mods:event ; text
    int f = 0, s = 0, key = 0, code, mods, event;
    unsigned char final = seq[seqlen - 1];
    int i;

    if (seq[1] == 'O') { // SS3: application cursor keys and F1-F4
        switch (final) {
        case 'A': key_event(KEY_UPARROW, EV_TAP, now); break;
        case 'B': key_event(KEY_DOWNARROW, EV_TAP, now); break;
        case 'C': key_event(KEY_RIGHTARROW, EV_TAP, now); break;
        case 'D': key_event(KEY_LEFTARROW, EV_TAP, now); break;
        case 'P': case 'Q': case 'R': case 'S':
            key_event(KEY_F1 + final - 'P', EV_TAP, now);
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
        break;
    case 'A': key = KEY_UPARROW; break;
    case 'B': key = KEY_DOWNARROW; break;
    case 'C': key = KEY_RIGHTARROW; break;
    case 'D': key = KEY_LEFTARROW; break;
    case 'P': case 'Q': case 'R': case 'S': key = KEY_F1 + final - 'P'; break;
    case '~':
        switch (code) {
        case 15: key = KEY_F5; break;
        case 17: key = KEY_F6; break;
        case 18: key = KEY_F7; break;
        case 19: key = KEY_F8; break;
        case 20: key = KEY_F9; break;
        case 21: key = KEY_F10; break;
        case 23: key = KEY_F11; break;
        case 24: key = KEY_F12; break;
        }
        break;
    }
    key_event(key, event, now);
}

void term_events(void)
{
    unsigned char b[128];
    int now, n, i, k;

    if (term_fd < 0)
        return;
    if (quit_signal)
        quit();
    now = I_GetTimeMS();
    while ((n = read(term_fd, b, sizeof b)) > 0) {
        for (i = 0; i < n; i++) {
            unsigned char c = b[i];

            if (seqlen > 0) {
                if (seqlen < (int) sizeof seq)
                    seq[seqlen++] = c;
                if (seqlen == 2 && c != '[' && c != 'O') { // Esc, then another key
                    seqlen = 0;
                    key_event(KEY_ESCAPE, EV_TAP, now);
                    i--;
                } else if (seqlen > 2 && c >= 0x40 && c <= 0x7e) {
                    sequence(now);
                    seqlen = 0;
                } else if (seqlen >= (int) sizeof seq) {
                    seqlen = 0;
                }
            } else if (c == 0x1b) {
                seq[0] = c;
                seqlen = 1;
                seq_at = now;
            } else if (c == 0x03) {
                quit();
            } else {
                key_event(doom_key(c), EV_TAP, now);
            }
        }
    }
    if (seqlen == 1 && now - seq_at >= ESC_WAIT_MS) {
        seqlen = 0;
        key_event(KEY_ESCAPE, EV_TAP, now);
    }
    for (k = 0; k < 256; k++) {
        if (until[k] && now - until[k] >= 0) {
            until[k] = 0;
            post(ev_keyup, k);
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

    if (!path || !*path)
        return;
    term_fd = open(path, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (term_fd < 0) {
        perror(path);
        return;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    is_tty = isatty(term_fd);
    if (is_tty && tcgetattr(term_fd, &saved) == 0) {
        t = saved;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(ICRNL | INLCR | IXON | BRKINT | ISTRIP);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(term_fd, TCSANOW, &t);
        restored = 0;
        I_AtExit(term_restore, true);
        put(TERM_ENTER);
        put(help);
        put(TERM_KEYS);
    }
    printf("I_InitInput: terminal input from %s\n", path);
}

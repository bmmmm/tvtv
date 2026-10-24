// SPDX-License-Identifier: GPL-3.0-or-later
// The snapshot reader (social.h). The file is read whole into a buffer one byte larger than a
// snapshot and decoded field by field, little-endian at the offsets of the bridge's
// docs/snapshot.md (no struct is laid over the bytes), and every rule there is checked, every
// program through dem0_load. A file that breaks one is turned down whole: the last valid
// snapshot stays, and the reason goes to stderr once per file. A file that is gone takes the
// snapshot with it (the bridge was stopped), and the social channels say KEIN EMPFANG.
// The rules are exactly those of the bridge's own check (internal/snapshot, Decode): what it
// writes, the show takes. Nothing here relies on the order of the event numbers in a list.
#include "social.h"
#include "core.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Offsets (snapshot.md, "Aufbau").
#define O_SEQ 16
#define O_CRC 24
#define O_FLAGS 28
#define O_WRITTEN 32
#define O_MODE 40
#define O_BOT 56
#define O_ANNOUNCE 120
#define O_CANVAS 128
#define O_PIXELS 3200
#define O_RECENT 3208
#define O_NPROG 3720
#define O_PROG 3724
#define PROG_SIZE 1868
#define O_NSLOTS 18668
#define O_SLOT 18672
#define SLOT_SIZE 160

static uint32_t le32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static uint64_t le64(const uint8_t *b) { return (uint64_t)le32(b) | (uint64_t)le32(b + 4) << 32; }

// CRC-32 as zlib has it (IEEE 802.3, reflected).
static uint32_t crc_add(uint32_t crc, const uint8_t *b, size_t n) {
    static uint32_t tab[256];
    if (!tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = tab[(crc ^ b[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static int zero(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (b[i]) return 0;
    return 1;
}

static int is_handle(int c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '@' || c == ':' || c == '-';
}
static int is_mode(int c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
static int is_text(int c) { return (c >= 0x20 && c <= 0x7E) || (c >= 0xA1 && c != 0xAD); } // Latin-1, as the fonts are
static int is_source(int c) { return c >= 0x20 && c <= 0x7E; }

// A string field of size bytes: the characters, then NUL up to its end, at least one. Returns
// its length, or -1 if a character is not allowed, anything but NUL follows the first NUL, or it
// is longer than max; only then is it copied to dst, which has room for max characters and NUL.
static int field(const uint8_t *b, int size, int (*ok)(int), char *dst, int max) {
    int n = 0;
    while (n < size && b[n]) {
        if (!ok(b[n])) return -1;
        n++;
    }
    if (n == size || n > max || !zero(b + n, (size_t)(size - n))) return -1;
    memcpy(dst, b, (size_t)n);
    dst[n] = 0;
    return n;
}
#define FIELD(b, size, ok, dst) field(b, size, ok, dst, (int)sizeof(dst) - 1)

// A handle, into a char[56]: 1 to 55 characters, the first an @.
static int handle(const uint8_t *b, int size, char dst[56]) {
    int n = field(b, size, is_handle, dst, 55);
    return n >= 1 && b[0] == '@';
}

static char why[160];
#define BAD(...)                                                                                                      \
    do {                                                                                                              \
        snprintf(why, sizeof why, __VA_ARGS__);                                                                       \
        return why;                                                                                                   \
    } while (0)

const char *social_parse(Social *s, const uint8_t *b, size_t n) {
    memset(s, 0, sizeof *s);
    if (n != SOCIAL_SIZE) BAD("size %zu, not %d", n, SOCIAL_SIZE);
    if (memcmp(b, "DEM0SNAP", 8)) BAD("magic");
    if (le32(b + 8) != 1) BAD("version %u", (unsigned)le32(b + 8));
    if (le32(b + 12) != SOCIAL_SIZE) BAD("size field %u", (unsigned)le32(b + 12));
    static const uint8_t none[4];
    uint32_t crc = crc_add(crc_add(crc_add(0, b, O_CRC), none, 4), b + O_CRC + 4, SOCIAL_SIZE - O_CRC - 4);
    if (crc != le32(b + O_CRC)) BAD("crc");
    s->seq = le64(b + O_SEQ);
    if (!s->seq) BAD("seq 0");
    s->flags = le32(b + O_FLAGS);
    if (s->flags & ~31u) BAD("flags %#x", (unsigned)s->flags);
    s->written_at = (int64_t)le64(b + O_WRITTEN);
    int len = FIELD(b + O_MODE, 16, is_mode, s->mode);
    if (len < 1) BAD("mode");
    if (!handle(b + O_BOT, 64, s->bot)) BAD("bot");
    s->announce_event = le32(b + O_ANNOUNCE);
    s->announce_kind = b[O_ANNOUNCE + 4];
    s->announce_item = b[O_ANNOUNCE + 5];
    s->pixel_secs = b[O_ANNOUNCE + 6];
    s->text_secs = b[O_ANNOUNCE + 7];
    if (s->pixel_secs < 5 || s->pixel_secs > 60) BAD("pixel_secs %d", s->pixel_secs);
    if (s->text_secs < 5 || s->text_secs > 60) BAD("text_secs %d", s->text_secs);

    for (int i = 0; i < SOCIAL_W * SOCIAL_H; i++)
        if (b[O_CANVAS + i] > 15) BAD("canvas: colour %d at %d %d", b[O_CANVAS + i], i % SOCIAL_W, i / SOCIAL_W);
    memcpy(s->canvas, b + O_CANVAS, sizeof s->canvas);
    s->pixel_count = le32(b + O_PIXELS);
    if (!zero(b + O_PIXELS + 4, 4)) BAD("reserved bytes at %d", O_PIXELS + 4);
    for (int i = 0; i < SOCIAL_RECENT; i++) {
        const uint8_t *e = b + O_RECENT + 64 * i;
        if (!e[3]) {
            if (!zero(e, 64)) BAD("recent[%d]: free, but not all 0", i);
            continue;
        }
        if (e[3] != 1) BAD("recent[%d]: used is %d", i, e[3]);
        if (i != s->nrecent) BAD("recent[%d]: used after a free entry", i);
        SocialPixel *p = &s->recent[i];
        p->x = e[0];
        p->y = e[1];
        p->color = e[2];
        p->event = le32(e + 4);
        if (p->x >= SOCIAL_W || p->y >= SOCIAL_H) BAD("recent[%d]: x %d y %d", i, p->x, p->y);
        if (p->color > 15) BAD("recent[%d]: colour %d", i, p->color);
        if (!p->event) BAD("recent[%d]: event 0", i);
        if (!handle(e + 8, 56, p->author)) BAD("recent[%d]: author", i);
        s->nrecent++;
    }

    s->nprog = b[O_NPROG];
    if (s->nprog > SOCIAL_PROGS) BAD("nprog %d", s->nprog);
    if (!zero(b + O_NPROG + 1, 3)) BAD("reserved bytes at %d", O_NPROG + 1);
    for (int i = 0; i < SOCIAL_PROGS; i++) {
        const uint8_t *e = b + O_PROG + PROG_SIZE * i;
        if (i >= s->nprog) {
            if (!zero(e, PROG_SIZE)) BAD("prog[%d]: unused, but not all 0", i);
            continue;
        }
        SocialProg *p = &s->prog[i];
        p->event = le32(e);
        p->secs = e[4];
        if (!p->event) BAD("prog[%d]: event 0", i);
        if (p->secs < 5 || p->secs > 60) BAD("prog[%d]: secs %d", i, p->secs);
        if (!zero(e + 5, 3)) BAD("prog[%d]: reserved bytes", i);
        if (!handle(e + 8, 56, p->author)) BAD("prog[%d]: author", i);
        len = FIELD(e + 64, 512, is_source, p->source);
        if (len < 1) BAD("prog[%d]: source", i);
        const char *err = dem0_load(&p->prog, e + 576);
        if (err) BAD("prog[%d]: object: %s", i, err);
    }

    s->nslots = b[O_NSLOTS];
    if (s->nslots > SOCIAL_SLOTS) BAD("nslots %d", s->nslots);
    if (!zero(b + O_NSLOTS + 1, 3)) BAD("reserved bytes at %d", O_NSLOTS + 1);
    for (int i = 0; i < SOCIAL_SLOTS; i++) {
        const uint8_t *e = b + O_SLOT + SLOT_SIZE * i;
        if (i >= s->nslots) {
            if (!zero(e, SLOT_SIZE)) BAD("slot[%d]: unused, but not all 0", i);
            continue;
        }
        SocialSlot *sl = &s->slot[i];
        len = FIELD(e, 24, is_text, sl->label);
        if (len < 1) BAD("slot[%d]: label", i);
        int used = FIELD(e + 24, 64, is_text, sl->text);
        if (used < 0) BAD("slot[%d]: text", i);
        len = FIELD(e + 88, 56, is_handle, sl->author);
        if (used ? !handle(e + 88, 56, sl->author) : len != 0) BAD("slot[%d]: author", i);
        sl->event = le32(e + 144);
        if (used ? !sl->event : sl->event != 0) BAD("slot[%d]: event", i);
        if (!zero(e + 148, 4)) BAD("slot[%d]: reserved bytes", i);
        sl->set_at = (int64_t)le64(e + 152);
        if (!used && sl->set_at) BAD("slot[%d]: set_at of a free line", i);
    }

    // last, because it points into the lists
    int item = s->announce_item;
    switch (s->announce_kind) {
    case 0:
        if (s->announce_event || item) BAD("announce: no kind, but an event or item");
        break;
    case SOCIAL_KIND_PIXEL:
        if (!s->announce_event || item || !s->nrecent) BAD("announce: pixel");
        break;
    case SOCIAL_KIND_DEM0:
        if (!s->announce_event || item >= s->nprog) BAD("announce: program %d", item);
        break;
    case SOCIAL_KIND_TEXT:
        if (!s->announce_event || item >= s->nslots || !s->slot[item].text[0]) BAD("announce: line %d", item);
        break;
    default:
        BAD("announce_kind %d", s->announce_kind);
    }
    return NULL;
}

// ---------- the file ----------

static Social cur, fresh;
static int have;
static uint8_t raw[SOCIAL_SIZE + 1]; // one more: a longer file shows as one
static char error[200];
static double due;
static struct {
    int valid;
    dev_t dev;
    ino_t ino;
    off_t size;
    time_t sec;
    long nsec;
} seen;

// announcements: what the last snapshot said, and one that is new since then
static int marked, pending;
static uint32_t mark;
static uint64_t last_seq;
static SocialNews news;

const char *social_path(void) {
    const char *env = getenv("BC101_SOCIAL");
    return env && *env ? env : "/run/tvtv-social/show.bin";
}

static double mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#ifdef __APPLE__
#define MTIME(st) ((st).st_mtimespec)
#else
#define MTIME(st) ((st).st_mtim)
#endif

// Into raw, at most one byte more than a snapshot. Never blocks: a FIFO or a device in the
// file's place is no regular file, and a link is not followed.
static const char *slurp(const char *path, size_t *n) {
    int fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return strerror(errno);
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
        close(fd);
        return "not a regular file";
    }
    size_t len = 0;
    while (len < sizeof raw) {
        ssize_t r = read(fd, raw + len, sizeof raw - len);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        len += (size_t)r;
    }
    close(fd);
    *n = len;
    return NULL;
}

// The first snapshot since the start only sets the mark, so that a restart announces nothing
// old; so does one with a smaller seq, from a bridge that started afresh.
static void notice(const Social *s) {
    if (marked && s->seq >= last_seq && s->announce_event > mark && (s->flags & SOCIAL_ANNOUNCE)) {
        news.kind = s->announce_kind;
        news.event = s->announce_event;
        news.secs = s->announce_kind == SOCIAL_KIND_PIXEL ? s->pixel_secs : s->text_secs;
        if (s->announce_kind == SOCIAL_KIND_DEM0) {
            news.event = s->prog[s->announce_item].event;
            news.secs = s->prog[s->announce_item].secs;
        }
        pending = 1;
    }
    marked = 1;
    mark = s->announce_event;
    last_seq = s->seq;
}

int social_poll(int force) {
    double tn = mono();
    if (!force && tn < due) return 0;
    due = tn + 0.5;
    const char *path = social_path();
    struct stat st;
    if (stat(path, &st)) {
        if (have) fprintf(stderr, "social: %s: %s; the social channels go blank\n", path, strerror(errno));
        snprintf(error, sizeof error, "%s", strerror(errno));
        have = 0;
        seen.valid = 0;
        return 0;
    }
    if (seen.valid && seen.dev == st.st_dev && seen.ino == st.st_ino && seen.size == st.st_size &&
        seen.sec == MTIME(st).tv_sec && seen.nsec == MTIME(st).tv_nsec)
        return 0;
    seen.valid = 1;
    seen.dev = st.st_dev;
    seen.ino = st.st_ino;
    seen.size = st.st_size;
    seen.sec = MTIME(st).tv_sec;
    seen.nsec = MTIME(st).tv_nsec;
    size_t n = 0;
    const char *err = slurp(path, &n);
    if (!err) err = social_parse(&fresh, raw, n);
    if (err) {
        snprintf(error, sizeof error, "%s", err);
        fprintf(stderr, "social: %s turned down: %s%s\n", path, err, have ? "; the last valid one stays" : "");
        return 0;
    }
    error[0] = 0;
    notice(&fresh);
    cur = fresh;
    have = 1;
    return 1;
}

const Social *social_get(void) {
    social_poll(0);
    return have ? &cur : NULL;
}

const char *social_error(void) { return error; }

int social_announce(SocialNews *n) {
    social_poll(0);
    if (!pending) return 0;
    pending = 0;
    if (n) *n = news;
    return 1;
}

// ---------- the dem0 rotation ----------

static uint32_t cued, played;

void social_cue(uint32_t event) { cued = event; }

const SocialProg *social_next_prog(void) {
    const Social *s = social_get();
    if (!s || !s->nprog) return NULL;
    const SocialProg *p = NULL, *newest = NULL, *older = NULL;
    for (int i = 0; i < s->nprog; i++) {
        const SocialProg *q = &s->prog[i];
        if (cued && q->event == cued) p = q;
        if (!newest || q->event > newest->event) newest = q;
        if (played && q->event < played && (!older || q->event > older->event)) older = q;
    }
    cued = 0;
    // the cued one, else the next older than the last played, after the oldest the newest again
    if (!p) p = older ? older : newest;
    played = p->event;
    return p;
}

// ---------- no reception ----------

// Snow as on a set without a signal, the channel and KEIN EMPFANG.
void social_card(float t, int ch, const char *name) {
    (void)t;
    static uint16_t grey[64];
    if (!grey[63])
        for (int g = 0; g < 64; g++) grey[g] = rgb(g * 4, g * 4, g * 4);
    for (int i = 0; i < W * H; i += 2) {
        uint32_t r = rnd();
        buf[i] = grey[r & 63];
        buf[i + 1] = grey[(r >> 6) & 63];
    }
    const char *s = "KEIN EMPFANG";
    char sub[48];
    snprintf(sub, sizeof sub, "Kanal %d \xb7 %s", ch, name);
    int w = text_w(&fbig, s, 1);
    rect((W - w) / 2 - 14, H / 2 - 36, w + 28, 72, 0);
    text(&fbig, s, (W - w) / 2, H / 2 - 30, 1, rgb(255, 255, 255));
    text(&fsml, sub, (W - text_w(&fsml, sub, 1)) / 2, H / 2 + 8, 1, rgb(255, 220, 120));
}

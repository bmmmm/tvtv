// SPDX-License-Identifier: GPL-3.0-or-later
// The snapshot reader on the bench, for tests/social.bats; built with ASan and UBSan there.
//   social-check <file>...                each through social_parse: "ok seq=<n> ..." or "bad: <why>"
//   social-check watch <path> <file>...   moves each file to <path> in turn, as the bridge does, and
//                                         says what the show then holds: "<seq|none> <why or ok>";
//                                         a file "-" removes <path>
//   social-check rotate <file> <cue> <n>  the events of the next n programs the dem0 scene plays,
//                                         the program with event <cue> cued first (0: none)
//   social-check overrun <file>           a file turned down at prog[0]'s source: has anything been
//                                         written past that field, into the program's object?
//   social-check fuzz <file> <n> <seed>   n mutations of a valid snapshot, half of them with the CRC
//                                         made right again: nothing may crash; prints how many passed
//   social-check poll <path>              what the show holds with $BC101_SOCIAL at <path>, whatever
//                                         <path> is (a FIFO, a device, a symlink): "<seq|none> <why or ok>"
#include "social.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t data[SOCIAL_SIZE + 64];

static size_t load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    size_t n = fread(data, 1, sizeof data, f);
    fclose(f);
    return n;
}

static void summary(const Social *s) {
    printf("ok seq=%llu flags=%u mode=%s bot=%s pixels=%d progs=%d slots=%d announce=%d/%d/%u\n",
           (unsigned long long)s->seq, (unsigned)s->flags, s->mode, s->bot, s->nrecent, s->nprog, s->nslots,
           s->announce_kind, s->announce_item, (unsigned)s->announce_event);
}

static uint32_t crc32(const uint8_t *b, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= b[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

static uint32_t seed;
static uint32_t next(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

int main(int argc, char **argv) {
    static Social s;
    if (argc >= 4 && !strcmp(argv[1], "watch")) {
        setenv("BC101_SOCIAL", argv[2], 1);
        for (int i = 3; i < argc; i++) {
            char tmp[1024];
            snprintf(tmp, sizeof tmp, "%s.tmp", argv[2]);
            if (!strcmp(argv[i], "-")) {
                unlink(argv[2]);
            } else {
                size_t n = load(argv[i]);
                FILE *f = fopen(tmp, "wb");
                if (!f || fwrite(data, 1, n, f) != n || fclose(f) || rename(tmp, argv[2])) {
                    perror(tmp);
                    return 2;
                }
            }
            social_poll(1);
            const Social *g = social_get();
            const char *err = social_error();
            if (g) printf("%llu %s\n", (unsigned long long)g->seq, *err ? err : "ok");
            else printf("none %s\n", *err ? err : "ok");
        }
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "rotate")) {
        setenv("BC101_SOCIAL", argv[2], 1);
        if (atol(argv[3])) social_cue((uint32_t)atol(argv[3]));
        for (int i = 0; i < atoi(argv[4]); i++) {
            const SocialProg *p = social_next_prog();
            printf(i ? " %u" : "%u", p ? (unsigned)p->event : 0u);
        }
        printf("\n");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "poll")) {
        setenv("BC101_SOCIAL", argv[2], 1);
        social_poll(1);
        const Social *g = social_get();
        const char *err = social_error();
        if (g) printf("%llu %s\n", (unsigned long long)g->seq, *err ? err : "ok");
        else printf("none %s\n", *err ? err : "ok");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "overrun")) {
        size_t n = load(argv[2]);
        const char *err = social_parse(&s, data, n);
        const uint8_t *z = (const uint8_t *)&s.prog[0].prog;
        size_t k = 0;
        while (k < sizeof s.prog[0].prog && !z[k]) k++;
        printf("%s, %s\n", err ? err : "ok", k < sizeof s.prog[0].prog ? "written past the source" : "nothing past it");
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "fuzz")) {
        size_t n = load(argv[2]);
        if (n != SOCIAL_SIZE || social_parse(&s, data, n)) {
            fprintf(stderr, "%s: not a valid snapshot to start from\n", argv[2]);
            return 2;
        }
        static uint8_t orig[SOCIAL_SIZE];
        memcpy(orig, data, sizeof orig);
        long rounds = atol(argv[3]), passed = 0;
        seed = (uint32_t)atol(argv[4]) | 1;
        for (long r = 0; r < rounds; r++) {
            memcpy(data, orig, sizeof orig);
            int flips = 1 + (int)(next() % 8);
            for (int k = 0; k < flips; k++) {
                uint32_t at = next() % SOCIAL_SIZE;
                // mostly a byte, now and then a run of them
                if (next() % 16 == 0) {
                    size_t len = 1 + next() % 64;
                    if (len > SOCIAL_SIZE - at) len = SOCIAL_SIZE - at;
                    memset(data + at, (int)(next() & 255), len);
                } else {
                    data[at] = (uint8_t)next();
                }
            }
            if (r & 1) {
                memset(data + 24, 0, 4);
                uint32_t c = crc32(data, SOCIAL_SIZE);
                for (int k = 0; k < 4; k++) data[24 + k] = (uint8_t)(c >> (8 * k));
            }
            size_t len = SOCIAL_SIZE;
            if (next() % 64 == 0) len = next() % (SOCIAL_SIZE + 2); // a file too short or too long
            if (!social_parse(&s, data, len)) passed++;
        }
        printf("fuzz %ld rounds, %ld passed\n", rounds, passed);
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: social-check <file>... | watch <path> <file>... | rotate <file> <cue> <n> | fuzz <file> <n> <seed> | poll <path>\n");
        return 2;
    }
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        size_t n = load(argv[i]);
        const char *err = social_parse(&s, data, n);
        if (err) {
            printf("bad: %s\n", err);
            bad = 1;
        } else {
            summary(&s);
        }
    }
    return bad;
}

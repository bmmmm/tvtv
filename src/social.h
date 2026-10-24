// SPDX-License-Identifier: GPL-3.0-or-later
// The social channels' data (24 pixel, 25 dem0, 26 text): the snapshot that the bridge,
// tvtv-social, writes for the show, one file of 19 952 bytes with the pixel wall, up to 8 dem0
// programs and the text page (the bridge's docs/snapshot.md, normative). The show never talks to
// the bridge; it reads that file ($BC101_SOCIAL, default /run/tvtv-social/show.bin), and since
// everything in it comes from public posts, it checks every byte before anything of it is shown.
#pragma once
#include "dem0.h"

#include <stddef.h>
#include <stdint.h>

#define SOCIAL_SIZE 19952
#define SOCIAL_W 64
#define SOCIAL_H 48
#define SOCIAL_RECENT 8
#define SOCIAL_PROGS 8
#define SOCIAL_SLOTS 8
#define SOCIAL_CARD_SECS 10 // how long "KEIN EMPFANG" stands when there is nothing to show

enum { SOCIAL_PAUSED = 1, SOCIAL_ANNOUNCE = 2, SOCIAL_LOOP_PIXEL = 4, SOCIAL_LOOP_DEM0 = 8, SOCIAL_LOOP_TEXT = 16 };
enum { SOCIAL_KIND_PIXEL = 1, SOCIAL_KIND_DEM0, SOCIAL_KIND_TEXT }; // what an announcement is about

typedef struct {
    uint8_t x, y, color;
    uint32_t event;
    char author[56];
} SocialPixel;

typedef struct {
    uint32_t event;
    int secs;
    char author[56], source[481];
    Dem0 prog;
} SocialProg;

typedef struct {
    char label[24], text[64], author[56]; // a free line has an empty text and author
    uint32_t event;
    int64_t set_at;
} SocialSlot;

typedef struct {
    uint64_t seq;
    uint32_t flags, announce_event, pixel_count;
    int64_t written_at;
    int announce_kind, announce_item, pixel_secs, text_secs;
    char mode[16], bot[64];
    uint8_t canvas[SOCIAL_H][SOCIAL_W]; // C64 colours 0..15
    int nrecent, nprog, nslots;
    SocialPixel recent[SOCIAL_RECENT]; // newest first, like the programs
    SocialProg prog[SOCIAL_PROGS];
    SocialSlot slot[SOCIAL_SLOTS]; // in page order
} Social;

typedef struct {
    int kind, secs;
    uint32_t event; // for a program: its event, to cue it (social_cue)
} SocialNews;

const char *social_path(void);
const Social *social_get(void); // the last valid snapshot or NULL; looks at the file at most every 0.5 s
int social_poll(int force);     // look now (force) or when due: 1 if a new snapshot was taken
const char *social_error(void); // why the file was last turned down, "" if it was not
const char *social_parse(Social *s, const uint8_t *b, size_t n); // NULL if b is a valid snapshot, else why
int social_announce(SocialNews *n); // 1, once, for an announcement that is new since the last call
void social_cue(uint32_t event);    // the dem0 scene's next run plays the program with this event
const SocialProg *social_next_prog(void); // that one, else the next older one (round robin); NULL if none
void social_card(float t, int ch, const char *name); // no snapshot: snow and KEIN EMPFANG

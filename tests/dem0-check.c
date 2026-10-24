// SPDX-License-Identifier: GPL-3.0-or-later
// The dem0 VM on the bench, for tests/social.bats; built with ASan and UBSan there.
//   dem0-check vectors <file>   the conformance vectors (the bridge's docs/dem0.md, section 10):
//                               every program loads, every point is within the tolerance
//   dem0-check fuzz <n> <seed>  n random valid programs, run at odd points and now and then as a
//                               whole picture, and n mutated objects through dem0_load, those it
//                               takes run as well: nothing may crash
//   dem0-check pace <ms>...     a program of cost 1, in 1x1 cells to begin with, after pictures of
//                               these times ($BC101_DEM0_MS the limit): the cells after each
#include "core.h"
#include "dem0.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int same(float got, float want) {
    if (isnan(want)) return isnan(got);
    if (got == want) return 1;
    double d = fabs((double)got - want);
    return d <= 1e-3 * fmax(1, fabs(want));
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int vectors(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 2;
    }
    static char line[8192];
    static Dem0 p;
    char name[64] = "";
    int have = 0, progs = 0, points = 0, exact = 0, fails = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!strncmp(line, "prog ", 5)) {
            char hex[2 * DEM0_SIZE + 2];
            have = 0;
            if (sscanf(line + 5, "%63s %2585s", name, hex) != 2 || strlen(hex) != 2 * DEM0_SIZE) {
                printf("FAIL line %d: not a prog line\n", lineno);
                fails++;
                continue;
            }
            uint8_t obj[DEM0_SIZE];
            int okhex = 1;
            for (int i = 0; i < DEM0_SIZE; i++) {
                int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
                if (hi < 0 || lo < 0) okhex = 0;
                obj[i] = (uint8_t)(hi << 4 | lo);
            }
            const char *err = okhex ? dem0_load(&p, obj) : "not hex";
            if (err) {
                printf("FAIL %s: the object is turned down: %s\n", name, err);
                fails++;
                continue;
            }
            have = 1;
            progs++;
        } else if (!strncmp(line, "at ", 3)) {
            if (!have) continue; // its program already failed
            float v[6];
            char *s = line + 3, *end;
            int n = 0;
            while (n < 6) {
                v[n] = strtof(s, &end);
                if (end == s) break;
                s = end;
                n++;
            }
            if (n != 3 + p.nout) {
                printf("FAIL %s, line %d: %d values for %d outputs\n", name, lineno, n - 3, p.nout);
                fails++;
                continue;
            }
            float w[3];
            dem0_eval(&p, v[0], v[1], v[2], w);
            points++;
            int all = 1;
            for (int k = 0; k < p.nout; k++) all &= w[k] == v[3 + k] || (isnan(w[k]) && isnan(v[3 + k]));
            exact += all;
            for (int k = 0; k < p.nout; k++)
                if (!same(w[k], v[3 + k])) {
                    printf("FAIL %s at %.9g %.9g %.9g: output %d is %.9g, not %.9g\n", name, v[0], v[1], v[2], k, w[k],
                           v[3 + k]);
                    fails++;
                }
        } else {
            printf("FAIL line %d: unknown line\n", lineno);
            fails++;
        }
    }
    fclose(f);
    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("ok %d programs, %d points, %d of them to the bit\n", progs, points, exact);
    return 0;
}

// ---------- fuzz ----------

static uint32_t seed;
static uint32_t next(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static const uint8_t ops[] = {0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
                              0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x30, 0x31, 0x32,
                              0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x40};
static const uint8_t cost[256] = {[0x01] = 1, [0x02] = 1, [0x03] = 1, [0x10] = 1, [0x11] = 1, [0x12] = 1,
                                  [0x13] = 2, [0x14] = 4, [0x15] = 1, [0x16] = 1, [0x17] = 8, [0x18] = 8,
                                  [0x19] = 3, [0x1A] = 1, [0x1B] = 1, [0x1C] = 1, [0x1D] = 1, [0x1E] = 1,
                                  [0x1F] = 1, [0x20] = 1, [0x21] = 2, [0x22] = 2, [0x23] = 2, [0x24] = 2,
                                  [0x25] = 2, [0x30] = 1, [0x31] = 1, [0x32] = 2, [0x33] = 2, [0x34] = 2,
                                  [0x35] = 2, [0x36] = 3, [0x37] = 3, [0x38] = 3, [0x39] = 8, [0x3A] = 8,
                                  [0x3B] = 1, [0x3C] = 1, [0x3D] = 2, [0x3E] = 3, [0x40] = 1};

static const float odd[] = {0,       -0.0f,         1,           -1,           0.5f,        255,     256,
                            1e-45f,  1e-38f,        -1e-40f,     3.4e38f,      -3.4e38f,    1e30f,   -1e30f,
                            2147483648.0f, -2147483648.0f, 2147483520.0f, 4294967296.0f, 16777217.0f, 3.14159274f,
                            1e-7f,   88.7f,         -103.9f,     31,           32,          33,      -33};
#define NODD (int)(sizeof odd / sizeof odd[0])

static float any(void) {
    if (next() & 1) return odd[next() % NODD];
    uint32_t bits = next();
    float f;
    memcpy(&f, &bits, sizeof f);
    return isfinite(f) ? f : (float)(int32_t)next();
}

// A valid object: every read register written before, the cost summed and at most 400, no
// constant subnormal.
static void program(uint8_t *o) {
    memset(o, 0, DEM0_SIZE);
    int nconst = (int)(next() % 65), want = 1 + (int)(next() % 256), nins = 0, sum = 0;
    for (int i = 0; i < nconst; i++) {
        float k = any();
        if (fabsf(k) < 0x1p-126f) k = 0;
        memcpy(o + 12 + 4 * i, &k, 4);
    }
    uint32_t written = 0;
    int wr[32], nw = 0;
    while (nins < want) {
        uint8_t op = ops[next() % sizeof ops];
        if (!nw && op != 0x02 && (op != 0x01 || !nconst)) op = 0x02;
        if (op == 0x01 && !nconst) continue;
        if (sum + cost[op] > 400) break;
        int d = (int)(next() % 32), a = 0, b = 0;
        if (op == 0x01) a = (int)(next() % (uint32_t)nconst);
        else if (op == 0x02) a = (int)(next() % 7);
        else {
            a = wr[next() % (uint32_t)nw];
            if (op < 0x30 || op == 0x40) b = wr[next() % (uint32_t)nw];
            if (op == 0x40) d = wr[next() % (uint32_t)nw];
            if (op == 0x03 || (op >= 0x30 && op < 0x40)) b = 0;
        }
        uint8_t *c = o + 268 + 4 * nins;
        c[0] = op;
        c[1] = (uint8_t)d;
        c[2] = (uint8_t)a;
        c[3] = (uint8_t)b;
        if (!(written >> d & 1)) wr[nw++] = d;
        written |= 1u << d;
        sum += cost[op];
        nins++;
    }
    o[0] = 1;
    o[1] = next() & 1 ? 3 : 1;
    for (int k = 0; k < o[1]; k++) o[2 + k] = (uint8_t)wr[next() % (uint32_t)nw];
    o[5] = (uint8_t)nconst;
    o[6] = (uint8_t)nins;
    o[7] = (uint8_t)(nins >> 8);
    o[8] = (uint8_t)sum;
    o[9] = (uint8_t)(sum >> 8);
}

static float point(int k) {
    switch (next() % 4) {
    case 0: return (float)(next() % 360);
    case 1: return odd[next() % NODD];
    case 2: return any();
    default: return k == 2 ? (float)(next() % 100000) / 100.0f : (float)(next() % 288);
    }
}

static int fuzz(long rounds, uint32_t s0) {
    seed = s0 | 1;
    static Dem0 p;
    static uint8_t o[DEM0_SIZE];
    long taken = 0, pictures = 0;
    for (long r = 0; r < rounds; r++) {
        program(o);
        const char *err = dem0_load(&p, o);
        if (err) {
            printf("FAIL round %ld: a valid program is turned down: %s\n", r, err);
            return 1;
        }
        float w[3];
        for (int k = 0; k < 4; k++) dem0_eval(&p, point(0), point(1), point(2), w);
        if (r % 97 == 0) {
            dem0_start(&p);
            dem0_render(point(2));
            pictures++;
        }
        // the same object a little broken: dem0_load decides, and what it takes must run
        int flips = 1 + (int)(next() % 4);
        for (int k = 0; k < flips; k++) o[next() % DEM0_SIZE] = (uint8_t)next();
        if (!dem0_load(&p, o)) {
            taken++;
            dem0_eval(&p, point(0), point(1), point(2), w);
        }
    }
    printf("fuzz %ld programs, %ld pictures, %ld broken ones taken\n", rounds, pictures, taken);
    return 0;
}

static int pace(int n, char **ms) {
    static Dem0 p;
    static uint8_t o[DEM0_SIZE];
    o[0] = 1;     // version
    o[1] = 1;     // one output, r0
    o[6] = 1;     // one instruction,
    o[8] = 1;     // cost 1:
    o[268] = 0x02; // IN r0, x
    const char *err = dem0_load(&p, o);
    if (err) {
        printf("FAIL: %s\n", err);
        return 1;
    }
    dem0_start(&p);
    for (int i = 0; i < n; i++) {
        dem0_pace(atof(ms[i]));
        printf(i ? " %d" : "%d", dem0_cell());
    }
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "vectors")) return vectors(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "pace")) return pace(argc - 2, argv + 2);
    if (argc == 4 && !strcmp(argv[1], "fuzz")) return fuzz(atol(argv[2]), (uint32_t)atol(argv[3]));
    fprintf(stderr, "usage: dem0-check vectors <file> | fuzz <n> <seed> | pace <ms>...\n");
    return 2;
}

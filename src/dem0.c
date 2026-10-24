// SPDX-License-Identifier: GPL-3.0-or-later
// The dem0 VM: checks objects and runs them (the bridge's docs/dem0.md is normative; section
// numbers below are its). A program is straight-line code over 32 registers of float32, and it
// runs lane-wise: for a chunk of CHUNK cells every instruction is one tight loop over the chunk,
// so the dispatch is paid once per chunk instead of once per cell, and the registers (32 x CHUNK
// floats) stay in the L1 cache. The loops go 4 lanes at a time, all loads before all stores: the
// Pi 1's VFP takes 8 cycles for an addition, and 4 independent ones overlap where a lane at a time
// would wait for each (d may be a or b, so a store must not come before a load of its block).
// Every operation is total and free of undefined behaviour: floats
// become integers only after a range check, shifts work on uint32 with the arithmetic shift
// written out, nothing overflows a signed integer, nothing divides by zero.
// The canvas is computed in cells of s x s pixels, s from 1 2 3 4 6 8 9 12 (section 8): the
// smallest for which (cost + 4) * cells fits the budget ($BC101_DEM0_BUDGET cost units per
// picture, default 600000, calibrated on the Pi 1 with build/preview dem0 bench). A program that
// still takes longer than 14 ms ($BC101_DEM0_MS) gets the next coarser cells, also when it is late
// only now and then (dem0_pace); one too slow even in 12x12 cells stands still.
// There are no subnormal numbers (flush-to-zero, section 5), decided on the exact value before
// rounding: the Pi 1's VFP11 would hand every one to the kernel's support code, a thousand times
// slower. On ARM the VFP does it in RunFast mode, set for each pass and put back after; elsewhere,
// as on the x86 of the tests, FTZ, FMUL and FDIV do it step by step (SSE's own flush decides after
// rounding and keeps what rounds to 2^-126).
// Build with -ffp-contract=off (the Makefile does): a fused multiply-add rounds once where
// dem0 rounds twice.
#include "dem0.h"
#include "core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(sizeof(float) == 4 && sizeof(uint32_t) == 4, "dem0 needs IEEE binary32 floats");

#define CHUNK 64 // cells per pass; a multiple of 4, as every grid's cell count is
#define NREG 32
#define MAXCOST 400

enum { IN_X, IN_Y, IN_T, IN_U, IN_V, IN_R, IN_A, NIN };

enum {
    OP_CONST = 0x01, OP_IN, OP_MOV,
    OP_ADD = 0x10, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_MIN, OP_MAX, OP_POW, OP_ATAN2, OP_HYPOT,
    OP_STEP, OP_EQ, OP_NE, OP_LT, OP_LE, OP_AND, OP_OR, OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR,
    OP_NEG = 0x30, OP_ABS, OP_FLOOR, OP_CEIL, OP_ROUND, OP_FRACT, OP_SQRT, OP_SIN, OP_COS, OP_EXP,
    OP_LOG, OP_SIGN, OP_NOT, OP_BNOT, OP_RND,
    OP_SEL = 0x40,
};

// Formats (7.2): K constant, I input, U one register, B two, S two plus d itself.
enum { F_K = 1, F_I, F_U, F_B, F_S };

static const uint8_t op_fmt[256] = {
    [OP_CONST] = F_K, [OP_IN] = F_I, [OP_MOV] = F_U,
    [OP_ADD] = F_B, [OP_SUB] = F_B, [OP_MUL] = F_B, [OP_DIV] = F_B, [OP_MOD] = F_B, [OP_MIN] = F_B,
    [OP_MAX] = F_B, [OP_POW] = F_B, [OP_ATAN2] = F_B, [OP_HYPOT] = F_B, [OP_STEP] = F_B,
    [OP_EQ] = F_B, [OP_NE] = F_B, [OP_LT] = F_B, [OP_LE] = F_B, [OP_AND] = F_B, [OP_OR] = F_B,
    [OP_BAND] = F_B, [OP_BOR] = F_B, [OP_BXOR] = F_B, [OP_SHL] = F_B, [OP_SHR] = F_B,
    [OP_NEG] = F_U, [OP_ABS] = F_U, [OP_FLOOR] = F_U, [OP_CEIL] = F_U, [OP_ROUND] = F_U,
    [OP_FRACT] = F_U, [OP_SQRT] = F_U, [OP_SIN] = F_U, [OP_COS] = F_U, [OP_EXP] = F_U,
    [OP_LOG] = F_U, [OP_SIGN] = F_U, [OP_NOT] = F_U, [OP_BNOT] = F_U, [OP_RND] = F_U,
    [OP_SEL] = F_S,
};

static const uint8_t op_cost[256] = {
    [OP_CONST] = 1, [OP_IN] = 1, [OP_MOV] = 1,
    [OP_ADD] = 1, [OP_SUB] = 1, [OP_MUL] = 1, [OP_DIV] = 2, [OP_MOD] = 4, [OP_MIN] = 1,
    [OP_MAX] = 1, [OP_POW] = 8, [OP_ATAN2] = 8, [OP_HYPOT] = 3, [OP_STEP] = 1,
    [OP_EQ] = 1, [OP_NE] = 1, [OP_LT] = 1, [OP_LE] = 1, [OP_AND] = 1, [OP_OR] = 1,
    [OP_BAND] = 2, [OP_BOR] = 2, [OP_BXOR] = 2, [OP_SHL] = 2, [OP_SHR] = 2,
    [OP_NEG] = 1, [OP_ABS] = 1, [OP_FLOOR] = 2, [OP_CEIL] = 2, [OP_ROUND] = 2,
    [OP_FRACT] = 2, [OP_SQRT] = 3, [OP_SIN] = 3, [OP_COS] = 3, [OP_EXP] = 8,
    [OP_LOG] = 8, [OP_SIGN] = 1, [OP_NOT] = 1, [OP_BNOT] = 2, [OP_RND] = 3,
    [OP_SEL] = 1,
};

static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t le32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

// 7.3: every rule, and on the first violation the whole object is out.
const char *dem0_load(Dem0 *p, const uint8_t *o) {
    memset(p, 0, sizeof *p);
    int nout = o[1], nconst = o[5], nins = le16(o + 6), cost = le16(o + 8);
    if (o[0] != 1) return "version";
    if (nout != 1 && nout != 3) return "nout";
    if (nconst > DEM0_NCONST) return "too many constants";
    if (nins < 1 || nins > DEM0_NINS) return "number of instructions";
    if (o[10] || o[11]) return "reserved bytes";
    for (int i = 0; i < DEM0_NCONST; i++) {
        uint32_t bits = le32(o + 12 + 4 * i);
        if (i >= nconst) {
            if (bits) return "unused constant is not 0";
            continue;
        }
        float k;
        memcpy(&k, &bits, sizeof k);
        if (!isfinite(k)) return "constant is not finite";
        if (!(bits & 0x7F800000u) && bits & 0x007FFFFFu) return "constant is subnormal";
        p->k[i] = k;
    }
    uint32_t written = 0;
    int sum = 0;
    for (int i = 0; i < DEM0_NINS; i++) {
        const uint8_t *c = o + 268 + 4 * i;
        if (i >= nins) {
            if (c[0] | c[1] | c[2] | c[3]) return "unused instruction is not 0";
            continue;
        }
        int op = c[0], d = c[1], a = c[2], b = c[3];
        if (d >= NREG) return "register";
        if (op_fmt[op] == F_S && !(written >> d & 1)) return "register read before it is written"; // SEL reads d
        switch (op_fmt[op]) {
        case F_K:
            if (a >= nconst || b) return "constant operand";
            break;
        case F_I:
            if (a >= NIN || b) return "input operand";
            p->inputs |= (uint8_t)(1u << a);
            break;
        case F_U:
            if (a >= NREG || b) return "operand";
            if (!(written >> a & 1)) return "register read before it is written";
            break;
        case F_B:
        case F_S:
            if (a >= NREG || b >= NREG) return "operand";
            if (!(written >> a & 1) || !(written >> b & 1)) return "register read before it is written";
            break;
        default:
            return "unknown opcode";
        }
        written |= 1u << d;
        sum += op_cost[op];
        memcpy(p->code[i], c, 4);
    }
    for (int k = 0; k < 3; k++) {
        int r = o[2 + k];
        if (k >= nout) {
            if (r) return "unused output is not 0";
        } else if (r >= NREG || !(written >> r & 1)) {
            return "output register is never written";
        }
        p->out[k] = (uint8_t)r;
    }
    if (sum != cost) return "cost does not match the code";
    if (cost > MAXCOST) return "cost over 400";
    p->nout = (uint8_t)nout;
    p->nconst = (uint8_t)nconst;
    p->nins = (uint16_t)nins;
    p->cost = (uint16_t)cost;
    return NULL;
}

// ---------- the operations (5.1 to 5.4) ----------

#if defined(__arm__)
// RunFast: FZ (flush-to-zero, before rounding) and DN (default NaN), and no trap enabled, as the
// kernel leaves them. The VFP11 then does everything itself; the rest of the show keeps IEEE.
#define FTZ(e) (e)
#define FMUL(a, b) ((a) * (b))
#define FDIV(a, b) ((a) / (b))
static inline uint32_t fpu_dem0(void) {
    uint32_t old;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(old)::"memory");
    __asm__ volatile("vmsr fpscr, %0" ::"r"(old | 1u << 24 | 1u << 25) : "memory");
    return old;
}
static inline void fpu_back(uint32_t old) { __asm__ volatile("vmsr fpscr, %0" ::"r"(old) : "memory"); }
#else
// A sum below 2^-126 is exact, so flushing the rounded one is right. A product of two floats is
// exact in double; a quotient that is not 2^-126 is at least 2^-25 of it away, far more than the
// rounding to double moves it, and rounding that once more to float is correct (53 >= 2*24+2).
static inline float ftz(float a) { return fabsf(a) < 0x1p-126f ? a * 0.0f : a; }
static inline float ftz64(double e) { return fabs(e) < 0x1p-126 ? (float)(e * 0.0) : (float)e; }
#define FTZ(e) ftz(e)
#define FMUL(a, b) ftz64((double)(a) * (double)(b))
#define FDIV(a, b) ftz64((double)(a) / (double)(b))
static inline uint32_t fpu_dem0(void) { return 0; }
static inline void fpu_back(uint32_t old) { (void)old; }
#endif

// -0 as +0, for atan2 (5.2): the sign of a flushed zero is not defined, and atan2 alone shows it.
static inline float pz(float a) { return a == 0 ? 0.0f : a; }

static int32_t u2i(uint32_t u) { return u < 0x80000000u ? (int32_t)u : (int32_t)(u - 0x80000000u) - 0x7FFFFFFF - 1; }

// I(a), ToInt32 as in JavaScript: NaN and infinities are 0, otherwise truncated and taken modulo
// 2^32. From 2^31 on a float32 is an integer, m * 2^e with its 24-bit significand m and e >= 8:
// modulo 2^32 that is m << e in 32 bits, and 0 from e = 32 on (NaN and infinity too), negated for
// a negative a. Bits rather than fmodf, which costs as much as a dozen other operations: the cost
// table (7.2) has to hold for any operand.
static inline int32_t toi(float f) {
    if (fabsf(f) < 2147483648.0f) return (int32_t)f;
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    int e = (int)(b >> 23 & 0xFFu) - 150;
    if (e >= 32) return 0;
    uint32_t r = ((b & 0x7FFFFFu) | 0x800000u) << e;
    return u2i(b >> 31 ? 0u - r : r);
}

static inline float itof(uint32_t u) { return (float)u2i(u); }

// floorf, exactly, without a call into libm (VFPv2 has no instruction for it): beyond 2^23, NaN
// and infinity are their own floor, and 0 keeps its sign.
static inline float vfloor(float a) {
    if (!(fabsf(a) < 8388608.0f) || a == 0) return a;
    float f = (float)(int32_t)a;
    return f > a ? f - 1 : f;
}

static float sine[4097];
#define SINE_SCALE 651.8986469044033f // 4096 / 2 pi

// 5.3: the table with linear interpolation, p already scaled; also NaN and infinities give 0.
// Below 2^31, f = floor(p) is exact as an int32 (beyond 2^24 p is an integer already).
static inline float tsin(float p) {
    if (!(fabsf(p) < 2147483648.0f)) return 0;
    int32_t i = (int32_t)p;
    if ((float)i > p) i--;
    float q = FTZ(p - (float)i);
    uint32_t k = (uint32_t)i & 4095u;
    return FTZ(sine[k] + FMUL(FTZ(sine[k + 1] - sine[k]), q));
}

static inline float vpow(float a, float b) {
    if ((a < 0 && b != vfloor(b)) || (a == 0 && b < 0)) return 0;
    return FTZ(powf(a, b));
}

static inline float vrnd(float a) {
    uint32_t k = (uint32_t)toi(vfloor(a));
    k ^= k >> 16;
    k *= 0x7feb352du;
    k ^= k >> 15;
    k *= 0x846ca68bu;
    k ^= k >> 16;
    return (float)(k >> 8) / 16777216.0f;
}

static inline float vshr(float a, float b) {
    uint32_t u = (uint32_t)toi(a), s = (uint32_t)toi(b) & 31u, r = u >> s;
    if (u & 0x80000000u) r |= ~(0xFFFFFFFFu >> s);
    return itof(r);
}

static float reg[NREG][CHUNK], in[NIN][CHUNK];

// e of a and b for 4 lanes: the loads, then the 4 results, then the stores.
#define LANES_B(e)                                                                                                    \
    {                                                                                                                 \
        float *D = reg[c[1]];                                                                                         \
        const float *A = reg[c[2]], *B = reg[c[3]];                                                                   \
        for (int j = 0; j < n; j += 4) {                                                                              \
            float va[4] = {A[j], A[j + 1], A[j + 2], A[j + 3]}, vb[4] = {B[j], B[j + 1], B[j + 2], B[j + 3]}, vd[4]; \
            _Pragma("GCC unroll 4") for (int l = 0; l < 4; l++) {                                                     \
                float a = va[l], b = vb[l];                                                                           \
                vd[l] = (e);                                                                                          \
            }                                                                                                         \
            memcpy(D + j, vd, sizeof vd);                                                                             \
        }                                                                                                             \
    }                                                                                                                 \
    break
#define LANES_U(e)                                                                                                    \
    {                                                                                                                 \
        float *D = reg[c[1]];                                                                                         \
        const float *A = reg[c[2]];                                                                                   \
        for (int j = 0; j < n; j += 4) {                                                                              \
            float va[4] = {A[j], A[j + 1], A[j + 2], A[j + 3]}, vd[4];                                                \
            _Pragma("GCC unroll 4") for (int l = 0; l < 4; l++) {                                                     \
                float a = va[l];                                                                                      \
                vd[l] = (e);                                                                                          \
            }                                                                                                         \
            memcpy(D + j, vd, sizeof vd);                                                                             \
        }                                                                                                             \
    }                                                                                                                 \
    break

// One pass of the program over n lanes, a multiple of 4. dem0_load has checked every operand, so
// every index below is in range.
static void run(const Dem0 *p, int n) {
    for (int i = 0; i < p->nins; i++) {
        const uint8_t *c = p->code[i];
        switch (c[0]) {
        case OP_CONST: {
            float k = p->k[c[2]], *D = reg[c[1]];
            for (int j = 0; j < n; j++) D[j] = k;
            break;
        }
        case OP_IN: memcpy(reg[c[1]], in[c[2]], (size_t)n * sizeof(float)); break;
        case OP_MOV: memmove(reg[c[1]], reg[c[2]], (size_t)n * sizeof(float)); break;
        case OP_ADD: LANES_B(FTZ(a + b));
        case OP_SUB: LANES_B(FTZ(a - b));
        case OP_MUL: LANES_B(FMUL(a, b));
        case OP_DIV: LANES_B(b == 0 ? 0 : FDIV(a, b));
        case OP_MOD: LANES_B(b == 0 ? 0 : FTZ(a - FMUL(b, vfloor(FDIV(a, b)))));
        case OP_MIN: LANES_B(a < b ? a : b);
        case OP_MAX: LANES_B(a > b ? a : b);
        case OP_POW: LANES_B(vpow(a, b));
        case OP_ATAN2: LANES_B(FTZ(atan2f(pz(a), pz(b))));
        case OP_HYPOT: LANES_B(sqrtf(FTZ(FMUL(a, a) + FMUL(b, b))));
        case OP_STEP: LANES_B(b < a ? 0.0f : 1.0f);
        case OP_EQ: LANES_B(a == b ? 1.0f : 0.0f);
        case OP_NE: LANES_B(a != b ? 1.0f : 0.0f);
        case OP_LT: LANES_B(a < b ? 1.0f : 0.0f);
        case OP_LE: LANES_B(a <= b ? 1.0f : 0.0f);
        case OP_AND: LANES_B(a != 0 && b != 0 ? 1.0f : 0.0f);
        case OP_OR: LANES_B(a != 0 || b != 0 ? 1.0f : 0.0f);
        case OP_BAND: LANES_B(itof((uint32_t)toi(a) & (uint32_t)toi(b)));
        case OP_BOR: LANES_B(itof((uint32_t)toi(a) | (uint32_t)toi(b)));
        case OP_BXOR: LANES_B(itof((uint32_t)toi(a) ^ (uint32_t)toi(b)));
        case OP_SHL: LANES_B(itof((uint32_t)toi(a) << ((uint32_t)toi(b) & 31u)));
        case OP_SHR: LANES_B(vshr(a, b));
        case OP_NEG: LANES_U(-a);
        case OP_ABS: LANES_U(fabsf(a));
        case OP_FLOOR: LANES_U(vfloor(a));
        case OP_CEIL: LANES_U(ceilf(a));
        case OP_ROUND: LANES_U(vfloor(FTZ(a + 0.5f)));
        case OP_FRACT: LANES_U(FTZ(a - vfloor(a)));
        case OP_SQRT: LANES_U(a < 0 ? 0 : sqrtf(a));
        case OP_SIN: LANES_U(tsin(FMUL(a, SINE_SCALE)));
        case OP_COS: LANES_U(tsin(FTZ(FMUL(a, SINE_SCALE) + 1024.0f)));
        case OP_EXP: LANES_U(FTZ(expf(a)));
        case OP_LOG: LANES_U(a <= 0 ? 0 : FTZ(logf(a)));
        case OP_SIGN: LANES_U(a > 0 ? 1.0f : a < 0 ? -1.0f : 0.0f);
        case OP_NOT: LANES_U(a == 0 ? 1.0f : 0.0f);
        case OP_BNOT: LANES_U(itof(~(uint32_t)toi(a)));
        case OP_RND: LANES_U(vrnd(a));
        case OP_SEL: {
            float *D = reg[c[1]];
            const float *A = reg[c[2]], *B = reg[c[3]];
            for (int j = 0; j < n; j += 4) {
                float vc[4] = {D[j], D[j + 1], D[j + 2], D[j + 3]}, vd[4];
                for (int l = 0; l < 4; l++) vd[l] = vc[l] != 0 ? A[j + l] : B[j + l];
                memcpy(D + j, vd, sizeof vd);
            }
            break;
        }
        }
    }
}

// ---------- inputs, palette, tables ----------

// Section 4, in float32 exactly as written there, flushed as every step (5).
static void polar(float x, float y, float *u, float *v, float *r, float *a) {
    *u = FDIV(FTZ(x - 180.0f), 144.0f);
    *v = FDIV(FTZ(y - 144.0f), 144.0f);
    *r = sqrtf(FTZ(FMUL(*u, *u) + FMUL(*v, *v)));
    *a = FTZ(atan2f(pz(*v), pz(*u)));
}

static float utab[W], vtab[H];
static float rtab[W * H], atab[W * H]; // per pixel; atan2f is too dear for every cell and picture
static uint16_t vga[256];

static void tables(void) {
    static int ready;
    if (ready) return;
    ready = 1;
    for (int k = 0; k < 4096; k++) sine[k] = (float)sin(2 * 3.14159265358979323846 * k / 4096);
    sine[0] = sine[4096] = 0;
    float r, a, dummy;
    for (int x = 0; x < W; x++) polar((float)x, 0, &utab[x], &dummy, &r, &a);
    for (int y = 0; y < H; y++) polar(0, (float)y, &dummy, &vtab[y], &r, &a);
    // 6.1, the palette the VGA BIOS loads for mode 13h, in 6 bits per channel
    static const uint8_t ega[16][3] = {{0, 0, 0},    {0, 0, 42},   {0, 42, 0},   {0, 42, 42},
                                       {42, 0, 0},   {42, 0, 42},  {42, 21, 0},  {42, 42, 42},
                                       {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
                                       {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63}};
    static const uint8_t grey[16] = {0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63};
    static const uint8_t level[9][5] = {{0, 16, 31, 47, 63}, {31, 39, 47, 55, 63}, {45, 49, 54, 58, 63},
                                        {0, 7, 14, 21, 28},  {14, 17, 21, 24, 28}, {20, 22, 24, 26, 28},
                                        {0, 4, 8, 12, 16},   {8, 10, 12, 14, 16},  {11, 12, 13, 15, 16}};
    static const uint8_t hue[24][3] = {{0, 0, 4}, {1, 0, 4}, {2, 0, 4}, {3, 0, 4}, {4, 0, 4}, {4, 0, 3},
                                       {4, 0, 2}, {4, 0, 1}, {4, 0, 0}, {4, 1, 0}, {4, 2, 0}, {4, 3, 0},
                                       {4, 4, 0}, {3, 4, 0}, {2, 4, 0}, {1, 4, 0}, {0, 4, 0}, {0, 4, 1},
                                       {0, 4, 2}, {0, 4, 3}, {0, 4, 4}, {0, 3, 4}, {0, 2, 4}, {0, 1, 4}};
    for (int i = 0; i < 256; i++) {
        int c[3] = {0, 0, 0};
        if (i < 16) {
            for (int k = 0; k < 3; k++) c[k] = ega[i][k];
        } else if (i < 32) {
            c[0] = c[1] = c[2] = grey[i - 16];
        } else if (i < 248) {
            int ring = (i - 32) / 24, h = (i - 32) % 24;
            for (int k = 0; k < 3; k++) c[k] = level[ring][hue[h][k]];
        }
        for (int k = 0; k < 3; k++) c[k] = (c[k] << 2) | (c[k] >> 4);
        vga[i] = (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3);
    }
}

static void polar_tables(void) {
    static int ready;
    if (ready) return;
    ready = 1;
    float u, v;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) polar((float)x, (float)y, &u, &v, &rtab[y * W + x], &atab[y * W + x]);
}

uint16_t dem0_vga(int i) {
    tables();
    return vga[i & 255];
}

// ---------- the picture ----------

static const int cells[] = {1, 2, 3, 4, 6, 8, 9, 12};
#define NCELLS 8

static struct {
    Dem0 p;
    int ci, still;
    double limit, late, total, worst;
    int frames;
} V;

static uint16_t cellbuf[(W / 2) * (H / 2)]; // the picture in cells; cells of 1 go straight to buf

long dem0_budget(void) {
    const char *e = getenv("BC101_DEM0_BUDGET");
    long b = e ? strtol(e, NULL, 10) : 0;
    return b > 0 && b <= 1000000000L ? b : 600000;
}

static double clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

void dem0_start(const Dem0 *p) {
    tables();
    V.p = *p;
    long budget = dem0_budget();
    V.ci = NCELLS - 1;
    for (int i = 0; i < NCELLS; i++)
        if ((long)(p->cost + 4) * (W / cells[i]) * (H / cells[i]) <= budget) {
            V.ci = i;
            break;
        }
    if (p->inputs & (1 << IN_R | 1 << IN_A)) polar_tables();
    const char *e = getenv("BC101_DEM0_MS");
    V.limit = e && atof(e) > 0 ? atof(e) : 14.0;
    V.still = V.frames = 0;
    V.late = V.total = V.worst = 0;
}

// After a picture of ms milliseconds: one over the limit adds 16 for each time the limit it took
// (at most 64), one within the limit takes 1 away, and at 64 the cells get coarser. A program late
// on every picture steps down after 4; one late on every other picture (dem0 has no branches, but
// operands differ) soon after; one that is late once in a while because the Pi had other things to
// do stays where it is.
void dem0_pace(double ms) {
    if (V.still) return;
    V.frames++;
    V.total += ms;
    if (ms > V.worst) V.worst = ms;
    V.late = ms > V.limit ? V.late + 16 * fmin(ms / V.limit, 4) : fmax(V.late - 1, 0);
    if (V.late < 64) return;
    V.late = 0;
    int s = cells[V.ci];
    if (V.ci + 1 < NCELLS) {
        fprintf(stderr, "dem0: %.1f ms per picture in %dx%d cells, from now on %dx%d\n", ms, s, s, cells[V.ci + 1],
                cells[V.ci + 1]);
        V.ci++;
    } else {
        fprintf(stderr, "dem0: %.1f ms per picture even in 12x12 cells: the picture stands still\n", ms);
        V.still = 1;
    }
}

int dem0_cell(void) { return cells[V.ci]; }
int dem0_cost(void) { return V.p.cost; }

void dem0_stats(double *avg_ms, double *worst_ms, int *frames) {
    *avg_ms = V.frames ? V.total / V.frames : 0;
    *worst_ms = V.worst;
    *frames = V.frames;
}

// The inputs the program reads, for the next n cells of a grid gw cells wide; (cx, cy) moves on.
static void inputs(const Dem0 *p, int n, int s, int gw, int *cx, int *cy) {
    int xs[CHUNK], ys[CHUNK], x = *cx, y = *cy;
    for (int j = 0; j < n; j++) {
        xs[j] = x * s;
        ys[j] = y * s;
        if (++x == gw) {
            x = 0;
            y++;
        }
    }
    *cx = x;
    *cy = y;
    unsigned use = p->inputs;
    if (use & 1 << IN_X)
        for (int j = 0; j < n; j++) in[IN_X][j] = (float)xs[j];
    if (use & 1 << IN_Y)
        for (int j = 0; j < n; j++) in[IN_Y][j] = (float)ys[j];
    if (use & 1 << IN_U)
        for (int j = 0; j < n; j++) in[IN_U][j] = utab[xs[j]];
    if (use & 1 << IN_V)
        for (int j = 0; j < n; j++) in[IN_V][j] = vtab[ys[j]];
    if (use & 1 << IN_R)
        for (int j = 0; j < n; j++) in[IN_R][j] = rtab[ys[j] * W + xs[j]];
    if (use & 1 << IN_A)
        for (int j = 0; j < n; j++) in[IN_A][j] = atab[ys[j] * W + xs[j]];
}

// Section 6: one value is a VGA colour, three are red, green and blue; each I(w) & 255.
static void output(const Dem0 *p, int n, uint16_t *dst) {
    const float *w0 = reg[p->out[0]];
    if (p->nout == 1) {
        for (int j = 0; j < n; j++) dst[j] = vga[(uint32_t)toi(w0[j]) & 255u];
        return;
    }
    const float *w1 = reg[p->out[1]], *w2 = reg[p->out[2]];
    for (int j = 0; j < n; j++) {
        uint32_t r = (uint32_t)toi(w0[j]) & 255u, g = (uint32_t)toi(w1[j]) & 255u, b = (uint32_t)toi(w2[j]) & 255u;
        dst[j] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
    }
}

static void upscale(int s) {
    int gw = W / s, gh = H / s;
    for (int cy = 0; cy < gh; cy++) {
        uint16_t *row = buf + cy * s * W;
        const uint16_t *src = cellbuf + cy * gw;
        for (int cx = 0; cx < gw; cx++)
            for (int k = 0; k < s; k++) row[cx * s + k] = src[cx];
        for (int k = 1; k < s; k++) memcpy(row + k * W, row, W * sizeof *row);
    }
}

void dem0_render(float t) {
    int s = cells[V.ci];
    if (!V.still) {
        int gw = W / s, n = gw * (H / s), cx = 0, cy = 0;
        uint16_t *dst = s == 1 ? buf : cellbuf;
        double t0 = clock_ms();
        for (int j = 0; j < CHUNK; j++) in[IN_T][j] = t;
        uint32_t fpu = fpu_dem0();
        for (int c0 = 0; c0 < n; c0 += CHUNK) {
            int m = n - c0 < CHUNK ? n - c0 : CHUNK; // a multiple of 4: so is every H / s
            inputs(&V.p, m, s, gw, &cx, &cy);
            run(&V.p, m);
            output(&V.p, m, dst + c0);
        }
        fpu_back(fpu);
        dem0_pace(clock_ms() - t0);
    }
    if (s > 1) upscale(s);
}

void dem0_eval(const Dem0 *p, float x, float y, float t, float w[3]) {
    tables();
    uint32_t fpu = fpu_dem0(); // the inputs too: a subnormal one is 0 (5), and so is one computed
    x = FTZ(x);
    y = FTZ(y);
    t = FTZ(t);
    float u, v, r, a;
    polar(x, y, &u, &v, &r, &a);
    for (int j = 0; j < 4; j++) { // the same point in all 4 lanes of a block
        in[IN_X][j] = x;
        in[IN_Y][j] = y;
        in[IN_T][j] = t;
        in[IN_U][j] = u;
        in[IN_V][j] = v;
        in[IN_R][j] = r;
        in[IN_A][j] = a;
    }
    run(p, 4);
    fpu_back(fpu);
    for (int k = 0; k < 3; k++) w[k] = k < p->nout ? reg[p->out[k]][0] : 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// dem0, picture code from public posts: the bridge (tvtv-social) compiles a program to bytecode,
// the show checks that bytecode again (dem0_load) and runs it for every cell of the canvas
// (dem0_render). The language and the object format are the bridge's docs/dem0.md, which is
// normative for both sides; tests/dem0-vectors.txt holds its conformance vectors.
#pragma once
#include <stdint.h>

#define DEM0_SIZE 1292  // bytes of an object
#define DEM0_NCONST 64
#define DEM0_NINS 256

// A checked program: the object's fields, nothing in it can surprise the VM.
typedef struct {
    uint8_t nout, out[3], nconst;
    uint8_t inputs; // bit i: the program reads input i (x y t u v r a)
    uint16_t nins, cost;
    float k[DEM0_NCONST];
    uint8_t code[DEM0_NINS][4]; // op d a b
} Dem0;

const char *dem0_load(Dem0 *p, const uint8_t *obj); // DEM0_SIZE bytes: NULL if valid, else why not
void dem0_start(const Dem0 *p);                     // play this one from now on: picks the cell size
void dem0_render(float t);                          // its picture at time t into buf
int dem0_cell(void);                                // the cell size in use: 1 to 12 pixels square
int dem0_cost(void);                                // the cost of the program on air
void dem0_stats(double *avg_ms, double *worst_ms, int *frames); // VM time per picture since dem0_start
void dem0_pace(double ms);                          // after a picture of ms: coarser cells if late
long dem0_budget(void);                             // cost units per picture ($BC101_DEM0_BUDGET)
void dem0_eval(const Dem0 *p, float x, float y, float t, float w[3]); // one point, for the vectors
uint16_t dem0_vga(int i);                           // the VGA palette as RGB565

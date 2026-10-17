// SPDX-License-Identifier: GPL-3.0-or-later
// The scene table, shared by the sequencer (main.c) and the offscreen tool (tools/preview.c).
#pragma once
#include "core.h"

extern const Scene *const scene_loop[];  // the default loop, in play order
extern const Scene *const scene_extra[]; // selectable by name only
extern const int scene_nloop, scene_nextra;
const Scene *scene_find(const char *name); // loop and extra; NULL if unknown
void scene_list(void);                     // print names and durations

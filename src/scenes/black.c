// SPDX-License-Identifier: GPL-3.0-or-later
// Standby: a black picture, channel 0 on the remote. Held, it keeps the tube dark without
// stopping the service; the sequencer's fades make its repeats invisible.
#include "core.h"

static void frame(float t, float dt) {
    (void)t;
    (void)dt;
    clear(0);
}

const Scene scene_black = {"black", 60, NULL, frame, NULL};

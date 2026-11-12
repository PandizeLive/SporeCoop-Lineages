// The revive ring drawn on screen (ring_timer_logic.h has the geometry; docs/re/ui.md#ui-revive-ring).
#pragma once

#include "ring_timer_logic.h"
#include "sporekit.h"

namespace ring_timer {
// Only during SK_EV_OVERLAY. (x, y): the centre in screen pixels; `left`: the fraction of the time still left (1 at the
// knockout, 0 when it runs out); `revive`: a partner's revive so far (0..1, 0: none); `now`: for the last quarter's
// blink. Nothing is drawn for a position that is not finite.
void draw(const SkHost* host, float x, float y, float radius, float left, float revive, double now);

// BUG-9156: a red edge round the screen (only during SK_EV_OVERLAY), strength 0..1
void vignette(const SkHost* host, int sw, int sh, float strength);
// BUG-9156: a filled arrow pointing left or right, centred at (x, y), `size` its half height
void arrow(const SkHost* host, float x, float y, float size, bool left, uint32_t argb);

// `coop.fix ui.revive_ring` (on): the ring instead of the text countdowns (off: "KO 12", "[REVIVE 12s]", "Downed: 12 s")
bool enabled();
} // namespace ring_timer

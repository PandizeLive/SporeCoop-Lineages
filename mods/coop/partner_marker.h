#pragma once
#include "sporekit.h"

namespace partner_marker {
// Screen-space filled arrow; only call during SK_EV_OVERLAY.
void draw(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned color);
}

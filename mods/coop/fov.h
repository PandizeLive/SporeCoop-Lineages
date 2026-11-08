#pragma once

#include "sporekit.h"

namespace fov {

bool init(const SkHost* host, SkMod* mod, bool (*input_allowed)());
void shutdown();

} // namespace fov

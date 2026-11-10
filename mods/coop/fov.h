#pragma once

#include <string>

#include "sporekit.h"

namespace fov {

// notify shows a local message (the change of the field of view)
bool init(const SkHost* host, SkMod* mod, bool (*input_allowed)(), void (*notify)(const std::string&));
void shutdown();

} // namespace fov

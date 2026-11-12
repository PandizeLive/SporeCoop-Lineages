#pragma once

#include <string>

#include "sporekit.h"

namespace fov {

// notify shows a local message (the change of the field of view)
bool init(const SkHost* host, SkMod* mod, bool (*input_allowed)(), void (*notify)(const std::string&));
void shutdown();

// BUG-9156: fn (null: none) moves the camera this frame (true) from the own creature to a partner, after its update
void set_camera_offset(bool (*fn)(float from[3], float to[3])); // the own creature's position, the partner's
} // namespace fov

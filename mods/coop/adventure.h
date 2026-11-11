#pragma once

#include <string>
#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure {
void init(const SkHost*, SkMod*, net::Session*);
void tick();
void message(uint8_t from, net::Reader&);
void reset();
net::AdventureGate outgoing();
std::string status(); // BUG-9089: the shared mission's counters (coop.adventure)
std::string debug_kill(uint32_t scenario_class); // tests: coop.adventure kill <class>
}

#pragma once

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure {
void init(const SkHost*, SkMod*, net::Session*);
void tick();
void message(uint8_t from, net::Reader&);
void reset();
net::AdventureGate outgoing();
}

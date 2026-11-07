#pragma once
#include "net/session.h"
#include "sporekit.h"
#include <string>
#include <vector>

namespace library {
void init(const SkHost *host, SkMod *mod, net::Session *session);
void tick(double now, bool safe);
void on_message(uint8_t from, net::Reader &reader);
void on_session_end();
void on_player_leave(uint8_t peer);
std::string debug(const std::vector<std::string> &args); // outside Present
} // namespace library

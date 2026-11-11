// BUG-9129: a guest's own pack, run by the host. The game has one pack (cPosseSimulator, the avatar's posse); in
// a session the host owns every NPC, so a guest's Summon Flock and recruiting made nothing. The host now keeps a
// pack per guest: its game's creatures (summoned or recruited for that guest) stay leashed to the guest's puppet,
// attack what the guest attacks and defend it; the guest sees them as the party's (NF_PACK: allies).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace guest_pack {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*(uint8_t id)> puppet_of; // host: a guest's puppet
    std::function<void(const std::string&)> notice;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
bool enabled(); // `coop.fix creature.guest_pack`

// host
int summon(uint8_t guest);              // the guest's Summon Flock: the game's flock, made that guest's (members)
bool recruit(uint8_t guest, void* npc); // the guest recruited host NPC `npc`
bool member(void* c);                   // in some guest's pack
int owner(void* c);                     // its guest, -1 none
void assist(uint8_t guest, void* victim);   // the guest hit `victim`: its pack attacks it
void defend(uint8_t guest, void* attacker); // `attacker` hit the guest's puppet: its pack attacks it
void tick(double now);                  // every frame (host)
void on_player_leave(uint8_t id);
void reset();                           // session end, world change
void set_herd_fix(bool on);             // tests
std::string status();

} // namespace guest_pack

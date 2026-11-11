#include "creature_rescue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "fixes.h"
#include "game.h"
#include "give_up.h"
#include "modes.h"
#include "net/interp.h"

namespace creature_rescue {
namespace {
// BUG-9118: 30 s for a partner to come (10 before); the downed player can give up (hold G 1 s): the
// same as the timer running out, at once. `coop.fix creature.down_30 off`: 10 s.
bool g_long_fix = true;
COOP_FIX("creature.down_30", "BUG-9118", g_long_fix, "creature / adventure: a downed player waits 30 s for a partner (10 before)");
double window_s() { return g_long_fix ? 30.0 : 10.0; }
constexpr double hold = 1.5;
constexpr float reach = 6;
enum Event : uint8_t { Begin, Revived, Expired, GiveUp };
const SkHost* H;
net::Session* S;
void (*notice)(const std::string&, int);
uint32_t planet, serial, local_id;
void* body;
net::Vec3 anchor;
double local_start, last_request, protection_until;
net::PlayerState peer;
bool has_peer;
bool g_leave_fix = true;
// BUG-9089 (44): a captain in an adventure is revived like a Creature-stage avatar (a partner has 10 s);
// if nobody comes, it gets back up beside a partner who is still standing, and only a captain whose
// partners are all down dies (the host's mission then fails for everyone, adventure.cpp).
bool g_adventure_fix = true;
COOP_FIX("adventure.revive", "BUG-9089", g_adventure_fix,
         "adventure: a downed captain is revived by a partner within 10 s, or gets back up beside one");
bool here(uint32_t mode) { return mode == modes::kCreature || (g_adventure_fix && mode == modes::kScenario); }
COOP_FIX("creature.recovery_leave", "BUG-9010", g_leave_fix,
         "creature: an existing death timer survives a partner leaving");

// The host tracks each victim independently, including simultaneous knockouts.
struct Rescue {
    uint32_t id = 0;
    bool active = false;
    double begin = 0, near_since = -1;
    std::string name;
    bool gave_up = false; // BUG-9118: the downed player gave up (as if the timer ran out)
};
bool local_give_up; // this game's downed player asked to give up (sent / applied by the next tick)
Rescue watches[net::kMaxPlayers];
// A departed seat may be reused before its timer expires. Keep only the old
// episode's deadline and name, never an avatar pointer or the reusable seat.
std::vector<Rescue> departed;

void send(uint8_t to, Event event, uint32_t id)
{
    net::Writer w;
    w.u32(planet); w.u32(id); w.u8(event);
    S->send_reliable_to(to, net::MSG_CREATURE_RESCUE, w);
}

void begin(uint8_t player, uint32_t id, double now)
{
    auto& w = watches[player];
    if (!w.active && (!w.id || int32_t(id - w.id) > 0))
        w = {id, true, now, -1, S->name_of(player)};
}

void charge(const std::string& name, bool gave_up = false)
{
    if (game::mode_id() == modes::kScenario) { // no DNA in an adventure
        notice(name + (gave_up ? " gave up." : " was down too long."), -1);
        return;
    }
    float dna = game::dna(), cost = 0;
    if (std::isfinite(dna) && dna >= 1) {
        cost = std::min(std::floor(dna), std::ceil(dna * 0.10f));
        game::dna_add_raw(-cost);
    }
    char loss[80];
    std::snprintf(loss, sizeof loss, " %s. The pack lost %.0f DNA.", gave_up ? "gave up" : "fell", cost);
    notice(name + loss, -1);
}

void resolve(Event result)
{
    local_id = 0;
    if (!body || !here(game::mode_id()) || game::avatar() != body)
        return;
    if (result == Expired && game::mode_id() == modes::kScenario && has_peer && peer.health > 0 &&
        !(peer.flags & (net::PF_DEAD | net::PF_DOWNED))) {
        // the adventure goes on: back up beside the partner who is still standing
        net::Vec3 at = peer.pos;
        at.x += 2.f;
        game::teleport(body, at, game::orientation(body));
        game::set_health(body, std::max(1.f, game::max_health(body) * 0.4f));
        protection_until = H->time_seconds() + 3;
        notice("Back on your feet beside " + S->name_of(peer.id) + ".", -1);
        return;
    }
    if (result == Revived) {
        game::set_health(body, std::max(1.f, game::max_health(body) * 0.4f));
        protection_until = H->time_seconds() + 2;
    } else {
        game::apply_damage(body, 1e6f, 0, nullptr); // native death and nest respawn
    }
}
}

void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int))
{
    H = host; S = session; notice = notify;
}

void reset()
{
    planet = local_id = 0;
    body = nullptr;
    has_peer = false;
    protection_until = 0;
    for (auto& w : watches) w = {};
    departed.clear();
}

bool down() { return local_id != 0; }
bool pending()
{
    if (down()) return true;
    for (const auto& w : watches) if (w.active) return true;
    return !departed.empty();
}

bool continuing() { return g_leave_fix && pending(); }

void on_player_leave(uint8_t player)
{
    if (!g_leave_fix) { reset(); return; }
    if (player >= net::kMaxPlayers) return;
    auto& w = watches[player];
    if (S->role() == net::Role::Host && w.active) departed.push_back(w);
    w = {};
    if (has_peer && peer.id == player) has_peer = false;
}

bool protect(void* avatar, float& damage)
{
    if (!S || !S->connected() || !here(game::mode_id()) || !planet ||
        avatar != body || !avatar || damage <= 0) return false;
    double now = H->time_seconds();
    if (down() || now < protection_until) { damage = 0; return true; }
    if (!has_peer || damage < game::health(avatar) || game::health(avatar) <= 0) return false;
    if (++serial == 0) ++serial;
    local_id = serial;
    anchor = game::position(body);
    local_start = now;
    last_request = now;
    game::set_health(body, 1);
    game::stop(body);
    damage = 0;
    if (S->role() == net::Role::Host) begin(0, local_id, now);
    else send(0, Begin, local_id);
    return true;
}

void message(uint8_t from, net::Reader& r)
{
    uint32_t world = r.u32(), id = r.u32();
    uint8_t event = r.u8();
    if (!r.ok() || r.left() || !id || !planet || world != planet || !S->connected() ||
        !here(game::mode_id()) || !body || game::avatar() != body) return;
    if (S->role() == net::Role::Host) {
        if (event == Begin && from < net::kMaxPlayers && from && has_peer && peer.id == from &&
            (peer.flags & net::PF_DOWNED))
            begin(from, id, H->time_seconds());
        else if (event == GiveUp && from && from < net::kMaxPlayers && watches[from].active && watches[from].id == id)
            watches[from].gave_up = true; // BUG-9118: settled by the next tick
    } else if (from == 0 && id == local_id && (event == Expired || event == Revived)) {
        resolve((Event)event);
    }
}

void tick(uint32_t world, void* avatar, const net::PlayerState* partner, double now)
{
    if (world != planet || avatar != body) reset();
    planet = world;
    body = avatar;
    has_peer = partner != nullptr;
    if (has_peer) peer = *partner;
    if (!planet || !body) return;
    if (down()) {
        game::set_health(body, 1); // BUG-9005: the game's nest also heals a downed avatar
        game::stop(body);
        if (net::dist(anchor, game::position(body)) > 0.4f)
            game::teleport(body, anchor, game::orientation(body));
        if (S->role() == net::Role::Client && now - last_request > 1) {
            send(0, Begin, local_id); // the first request may predate the host's first usable peer state
            last_request = now;
        }
        if (S->role() == net::Role::Client && !has_peer && now - local_start >= window_s() + 5) {
            resolve(Expired); // a host in another stage cannot resolve this body's knockout
            return;
        }
        // BUG-9118: giving up (hold G, or coop.giveup): the host settles it like an expired timer
        if (local_give_up || give_up::held(H, now)) {
            local_give_up = false;
            if (S->role() == net::Role::Client) send(0, GiveUp, local_id);
            else if (watches[0].active && watches[0].id == local_id) watches[0].gave_up = true;
        }
    } else
        local_give_up = false;
    if (S->role() != net::Role::Host) return;
    for (auto it = departed.begin(); it != departed.end();) {
        if (now - it->begin < window_s()) { ++it; continue; }
        charge(it->name);
        it = departed.erase(it);
    }
    for (uint8_t victim = 0; victim < net::kMaxPlayers; ++victim) {
        auto& w = watches[victim];
        if (!w.active) continue;
        bool near = has_peer && (!victim || victim == peer.id) &&
                    (victim ? !down() && game::health(body) > 0 :
                              !watches[peer.id].active && peer.health > 0 && !(peer.flags & (net::PF_DEAD | net::PF_DOWNED))) &&
                    net::dist(game::position(body), peer.pos) <= reach;
        if (!near) w.near_since = -1;
        else if (w.near_since < 0) w.near_since = now;
        bool saved = !w.gave_up && near && w.near_since + hold <= std::min(now, w.begin + window_s());
        if (!saved && !w.gave_up && now - w.begin < window_s()) continue;
        w.active = false; // one host debit per knockout, even if both players expire on this frame
        Event result = saved ? Revived : Expired;
        if (!saved) charge(w.name, w.gave_up);
        if (victim) send(victim, result, w.id);
        else resolve(result);
        if (saved) notice(w.name + " was revived.", -1);
    }
}

void give_up_now() { if (down()) local_give_up = true; } // tests and the console: coop.giveup

std::string status()
{
    if (down()) {
        int sec = std::max(0, (int)std::ceil(window_s() - (H->time_seconds() - local_start)));
        return "Downed: " + std::to_string(sec) + " s for your partner to revive you (hold G to give up)";
    }
    if (has_peer && (peer.flags & net::PF_DOWNED))
        return "Revive " + S->name_of(peer.id) + ": stay close for 1.5 s";
    return {};
}
}

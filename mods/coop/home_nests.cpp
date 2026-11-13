#include "home_nests.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "creations.h"
#include "fixes.h"
#include "founders.h"
#include "game.h"
#include "net/interp.h"
#include "party.h"
#include "social.h"
#include "sporekit_game.hpp"
#include "world.h"

namespace home_nests {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static bool g_fix = true;
COOP_FIX("creature.own_nests", "BUG-9135", g_fix,
         "creature: every player with their own species has their own nest next to the host's (hatch, respawn, migration)");

constexpr uint32_t kNestNoun = 0x52AA6122; // cNest
constexpr uint32_t kCrtType = 0x2B978C46, kCrtGroup = 0x40626200;
constexpr float kNear = 160.f;             // a seat's nest belongs to the host's current home within this
constexpr int kFamily = 4;                 // the members the game keeps in a seat's nest herd

struct SeatNest {
    void* herd = nullptr;
    net::Vec3 pos{};
    uint32_t species = 0;
};
static std::array<SeatNest, founders::kMaxSeats> g_nests;
static double g_next;
static bool g_queued;
static uint32_t g_made, g_failed, g_rebuilt, g_ally_set, g_shares, g_followed, g_follow_members;
// BUG-9151: a player who evolves (or picks another creature of their own) keeps their nest: the herd (+0xA4) and its
// family take the new species in place, as the game's own evolution does with the player's family (the guest's game
// mirrors these members). Before, the host found no nest of the new species, built a second one elsewhere, left the old
// one standing as an AI nest of the old body and sent the whole world again. Off: a new nest (0.7.8).
static bool g_follow = true;
COOP_FIX("creature.nest_follows_evolution", "BUG-9151", g_follow,
         "creature: a partner who evolves keeps their nest; its herd and family take the new species (no second nest, no world resend)");
static std::string g_last;

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
}

bool enabled() { return g_fix; }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }

static bool live_herd(void* h)
{
    return h && game::object_alive(h) && skg::readable(h, 0x21C) && game::noun_id(h) == game::kHerdNoun;
}

static bool nest_pos_of(void* herd, net::Vec3& out)
{
    void* nest = live_herd(herd) ? game::herd_nest(herd) : nullptr;
    return nest && game::object_alive(nest) && game::object_position(nest, out);
}

// the nest herd of `profile` nearest to `at` within `within` m that is not extinct (+0x190), or null
static void* nest_herd_near(void* profile, const net::Vec3& at, float within, net::Vec3* pos = nullptr)
{
    struct F {
        void* profile;
        net::Vec3 at;
        float best;
        void* herd = nullptr;
        net::Vec3 pos{};
    } f{profile, at, within};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto& f = *(F*)u;
            net::Vec3 q;
            if (noun == game::kHerdNoun && live_herd(o) && game::herd_species(o) == f.profile && !skg::field<uint8_t>(o, 0x190) &&
                nest_pos_of(o, q)) {
                float d = net::dist(q, f.at);
                if (d < f.best) {
                    f.best = d;
                    f.herd = o;
                    f.pos = q;
                }
            }
            return true;
        },
        &f);
    if (pos && f.herd)
        *pos = f.pos;
    return f.herd;
}

// decision 2: a flat spot 45-80 m from the host's home nest, at its height (within 6 m), 25 m from every other nest
static bool site_near(const net::Vec3& home, net::Vec3& out)
{
    const float r = std::sqrt(home.x * home.x + home.y * home.y + home.z * home.z);
    const float h0 = game::ground_radius(home);
    if (!(r > 1) || !std::isfinite(h0))
        return false;
    net::Vec3 n{home.x / r, home.y / r, home.z / r};
    net::Vec3 u{n.z, 0, -n.x};
    float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (ul < 1e-3f) {
        u = {0, -n.z, n.y};
        ul = std::sqrt(u.y * u.y + u.z * u.z);
    }
    u = {u.x / ul, u.y / ul, u.z / ul};
    net::Vec3 v{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
    std::vector<net::Vec3> nests;
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            net::Vec3 q;
            if (noun == kNestNoun && game::object_alive(o) && game::object_position(o, q))
                ((std::vector<net::Vec3>*)p)->push_back(q);
            return true;
        },
        &nests);
    for (float dist : {60.f, 50.f, 70.f, 45.f, 80.f})
        for (int k = 0; k < 12; k++) {
            const float a = (k * 30 + dist) * 0.0174533f;
            net::Vec3 q{home.x + dist * (u.x * std::cos(a) + v.x * std::sin(a)), home.y + dist * (u.y * std::cos(a) + v.y * std::sin(a)),
                        home.z + dist * (u.z * std::cos(a) + v.z * std::sin(a))};
            const float g = game::ground_radius(q), len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
            if (!std::isfinite(g) || std::fabs(g - h0) > 6 || len < 1)
                continue;
            q = {q.x * g / len, q.y * g / len, q.z * g / len};
            bool clear = true;
            for (const auto& o : nests)
                clear &= net::dist(o, q) >= 25.f;
            if (clear) {
                out = q;
                return true;
            }
        }
    return false;
}

static bool host_home(net::Vec3& out)
{
    void* av = game::avatar();
    return av && game::nest_position(av, out);
}

// the seat whose nest is missing near the host's home gets one (outside the frame: it creates game objects)
static void make_nest(void* ctx)
{
    const int seat = (int)(intptr_t)ctx;
    g_queued = false;
    if (!g_fix || !is_host() || !game::in_creature_stage() || game::cinematic_playing() || world::load_queued())
        return;
    const auto& roster = party::roster();
    if (seat <= 0 || seat >= (int)roster.seats.size())
        return;
    const uint32_t species = roster.seats[seat].species;
    void* profile = species ? game::species_by_key({species, kCrtType, kCrtGroup}) : nullptr;
    net::Vec3 home, site;
    if (!profile || !host_home(home) || nest_herd_near(profile, home, kNear))
        return;
    if (!site_near(home, site)) {
        if (g_failed++ < 5)
            H->log(M, SK_WARN, "home nests: no flat spot 45-80 m from the host's nest for seat %d", seat);
        return;
    }
    void* herd = game::create_nest_herd(site, profile, kFamily, false);
    net::Vec3 at;
    if (!herd || !nest_pos_of(herd, at)) {
        if (g_failed++ < 5)
            H->log(M, SK_WARN, "home nests: the game made no nest herd for seat %d (species %08X)", seat, species);
        return;
    }
    // the host's nest's brain range: the relevance pass disables a nest herd whose range misses the brain level
    if (void* own = game::avatar_herd(); live_herd(own)) {
        skg::field<int32_t>(herd, 0x120) = skg::field<int32_t>(own, 0x120);
        skg::field<int32_t>(herd, 0x124) = skg::field<int32_t>(own, 0x124);
    }
    static uintptr_t best = H->sym("SporeCoop::cHerd_SetBestFriends");
    if (best)
        skg::thiscall<void>(best, herd, true);
    if (social::set_party_ally(species)) // decision 7: the party's species are best friends
        g_ally_set++;
    // an offline player's nest must load without them: their creature goes with the save (as a founder's)
    std::string err;
    const uint8_t player = party::player_of_seat(seat);
    if (player != party::kNobody && !creations::archive_guest_model(player, world::current_game_key(), err))
        H->log(M, SK_WARN, "home nests: seat %d's creature not archived: %s", seat, err.c_str());
    const bool rebuilt = g_nests[seat].species == species;
    (rebuilt ? g_rebuilt : g_made)++;
    g_nests[seat] = {herd, at, species};
    char b[200];
    snprintf(b, sizeof b, "home nests: seat %d (%s) has its own nest of %08X at (%.0f %.0f %.0f), %.0f m from the host's", seat,
             roster.seats[seat].name.c_str(), species, at.x, at.y, at.z, net::dist(at, home));
    g_last = b;
    H->log(M, SK_INFO, "%s", b);
    if (C.notice && !roster.seats[seat].name.empty())
        C.notice(roster.seats[seat].name + "'s species has its own nest now");
    // the player's world is a copy of ours: without the new nest in it their game has no home to hatch at (and an
    // empty local copy faulted the game's claim). An online player gets the world again, once per new nest.
    if (player != party::kNobody && C.sess && player != C.sess->my_id()) {
        std::string share_err;
        if (world::share_with(player, share_err, true))
            g_shares++;
        else
            H->log(M, SK_WARN, "home nests: seat %d's world not sent again: %s", seat, share_err.c_str());
    }
}

// seat `ctx`'s nest takes its player's new species (outside the frame: SetSpecies rebuilds models)
static void follow_species(void* ctx)
{
    const int seat = (int)(intptr_t)ctx;
    g_queued = false;
    if (!g_fix || !g_follow || !is_host() || !game::in_creature_stage() || game::cinematic_playing() || world::load_queued())
        return;
    const auto& roster = party::roster();
    if (seat <= 0 || seat >= (int)roster.seats.size() || seat >= (int)g_nests.size())
        return;
    SeatNest& n = g_nests[seat];
    const uint32_t species = roster.seats[seat].species;
    void* profile = species ? game::species_by_key({species, kCrtType, kCrtGroup}) : nullptr;
    net::Vec3 at;
    if (!profile || !n.species || n.species == species || !nest_pos_of(n.herd, at))
        return;
    void* old_profile = game::herd_species(n.herd);
    skg::field<void*>(n.herd, 0xA4) = profile; // the herd's species (a cSpeciesProfile of the species manager)
    static const uintptr_t set_species = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    struct X {
        void *herd, *prof;
        uintptr_t set;
        uint32_t changed;
    } x{n.herd, profile, set_species, 0};
    if (set_species)
        game::for_each_object([](void* creature, uint32_t noun, void* p) {
            auto& x = *static_cast<X*>(p);
            if (noun != 0x18EB45Eu || game::is_puppet(creature) || !game::object_alive(creature) || game::is_dead(creature) ||
                game::herd_of(creature) != x.herd || game::species_profile(creature) == x.prof)
                return true;
            const int age = *(int*)((uint8_t*)creature + 0xB34);
            skg::thiscall<bool>(x.set, creature, x.prof, age, true);
            ++x.changed;
            return true;
        }, &x);
    if (social::set_party_ally(species))
        g_ally_set++;
    std::string err;
    const uint8_t player = party::player_of_seat(seat);
    if (player != party::kNobody && !creations::archive_guest_model(player, world::current_game_key(), err))
        H->log(M, SK_WARN, "home nests: seat %d's new creature not archived: %s", seat, err.c_str());
    char b[220];
    snprintf(b, sizeof b, "home nests: seat %d (%s)'s nest at (%.0f %.0f %.0f) follows its evolution %08X -> %08X (herd %p was %p, %u member(s), BUG-9151)",
             seat, roster.seats[seat].name.c_str(), at.x, at.y, at.z, n.species, species, n.herd, old_profile, x.changed);
    g_last = b;
    H->log(M, SK_INFO, "%s", b);
    n.species = species;
    g_followed++;
    g_follow_members += x.changed;
}

void host_tick(double now)
{
    if (!g_fix || !is_host() || now < g_next || g_queued)
        return;
    g_next = now + 2;
    if (!game::in_creature_stage() || game::cinematic_playing() || world::time_paused() || world::load_queued())
        return;
    net::Vec3 home;
    if (!host_home(home))
        return;
    const auto& roster = party::roster();
    const uint32_t host_species = roster.seats.empty() ? 0 : roster.seats[0].species;
    for (size_t seat = 1; seat < roster.seats.size(); ++seat) {
        const uint32_t species = roster.seats[seat].species;
        if (!species || species == host_species) {
            g_nests[seat] = {}; // plays the host's species: the host's nest
            continue;
        }
        void* profile = game::species_by_key({species, kCrtType, kCrtGroup});
        if (!profile)
            continue; // its creature is not here (yet)
        net::Vec3 at;
        if (void* herd = nest_herd_near(profile, home, kNear, &at)) {
            g_nests[seat] = {herd, at, species};
            continue;
        }
        // BUG-9151: its nest of the species it played before is still here: that nest follows (one a tick)
        if (g_follow && g_nests[seat].species && g_nests[seat].species != species && nest_pos_of(g_nests[seat].herd, at) &&
            net::dist(at, home) < kNear && party::player_of_seat((int)seat) != party::kNobody) {
            g_queued = true;
            H->run_outside_frame(follow_species, (void*)(intptr_t)seat);
            return;
        }
        // none near the host's home: a new player, a migration (decision 3), an extinct nest (decision 8). Made only while
        // the seat's player is here: an offline player's nest is the AI's (it may die out) and is made again on return.
        if (party::player_of_seat((int)seat) == party::kNobody)
            continue;
        g_queued = true;
        H->run_outside_frame(make_nest, (void*)(intptr_t)seat);
        return; // one a tick
    }
}

bool home_for(uint8_t player, net::Vec3& out)
{
    if (g_fix && is_host()) {
        const int seat = party::seat_of_player(player);
        if (seat > 0 && seat < (int)g_nests.size() && nest_pos_of(g_nests[seat].herd, out))
            return true;
    }
    return host_home(out);
}

void on_world_change()
{
    g_nests = {};
    g_queued = false;
    g_next = 0;
}

std::string status()
{
    char b[220];
    snprintf(b, sizeof b, "home nests: fix=%d made=%u rebuilt=%u followed=%u (%u members) failed=%u ally_set=%u world_resent=%u last: %s\n",
             (int)g_fix, g_made, g_rebuilt, g_followed, g_follow_members, g_failed, g_ally_set, g_shares,
             g_last.empty() ? "-" : g_last.c_str());
    std::string out = b;
    net::Vec3 home;
    const bool have_home = host_home(home);
    const auto& roster = party::roster();
    for (size_t i = 1; i < g_nests.size(); ++i) {
        net::Vec3 p;
        if (!nest_pos_of(g_nests[i].herd, p)) {
            if (i < roster.seats.size() && roster.seats[i].species) {
                const uint32_t sp = roster.seats[i].species;
                void* prof = game::species_by_key({sp, kCrtType, kCrtGroup});
                snprintf(b, sizeof b, "  seat %zu: species %08X (profile %p, player %d), no nest of its own (kept herd %p alive %d extinct %d)\n",
                         i, sp, prof, (int)party::player_of_seat((int)i), g_nests[i].herd, (int)live_herd(g_nests[i].herd),
                         live_herd(g_nests[i].herd) ? (int)skg::field<uint8_t>(g_nests[i].herd, 0x190) : -1);
                out += b;
            }
            continue;
        }
        snprintf(b, sizeof b, "  seat %zu: species %08X nest (%.0f %.0f %.0f) %.0f m from the host's, members %d\n", i, g_nests[i].species,
                 p.x, p.y, p.z, have_home ? net::dist(p, home) : -1.f,
                 (int)((skg::field<uintptr_t>(g_nests[i].herd, 0x44) - skg::field<uintptr_t>(g_nests[i].herd, 0x40)) / 4));
        out += b;
    }
    return out;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[0] == "make" && is_host()) { // tests: make seat N's nest now (when it has none near)
        g_queued = true;
        H->run_outside_frame(make_nest, (void*)(intptr_t)atoi(a[1].c_str()));
        return "queued\n";
    }
    if (a.size() >= 2 && a[0] == "extinct" && is_host()) { // tests (decision 8): seat N's nest dies out (the game's own call)
        const int seat = atoi(a[1].c_str());
        static uintptr_t extinct = H->sym("SporeCoop::cHerd_SetExtinct");
        if (seat <= 0 || seat >= (int)g_nests.size() || !live_herd(g_nests[seat].herd) || !extinct)
            return "no nest for that seat\n";
        skg::thiscall<void>(extinct, g_nests[seat].herd);
        return "extinct\n";
    }
    return status();
}

} // namespace home_nests

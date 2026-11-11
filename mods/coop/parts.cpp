#include "parts.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

namespace parts {

using namespace skg;

static const SkHost* H;
static SkMod* M;
static Ctx C;

// ---------------------------------------------------------------- BUG-0401
// A part a player finds (a skeleton's reward, a creature's drop, an evolution reward) is unlocked in
// that player's own game only: the creature stage's event handler 0xD47FC0 picks it (0xD47E50) and
// cCollectableItems 0x5976D0 -> 0x597410 sets the item's state (no UnlockPart call); loads and the
// cell->creature start go through UnlockPart 0x596DA0 (docs/re/creations.md#creations-part-pickup).
// Nothing sent a part to the other games: a guest's own editor (BUG-0368: the guest's collected parts)
// never offered the parts the host found, and the parts a guest found were lost at its next world
// load (the host's save). Now the party shares one set: every game polls its collected parts (1 Hz),
// a guest sends the parts it has and the host's set lacks to the host, the host unlocks them in its
// game (UnlockPart, cost 0) and sends its whole set to every guest (at once on a change, else every
// 10 s), a guest that plays the host's world unlocks what it lacks. Nothing is ever locked.
// `coop.fix parts.shared off`: every game keeps its own parts (0.7.3.3).
static bool g_fix = true;
COOP_FIX("parts.shared", "BUG-0401", g_fix, "creature: the parts a player finds are unlocked for the whole party (host-held union)");

// MSG_PARTS: u8 kind, u8 origin player, u32 world (the sender's save CRC), u16 n, n x {u32 lo, u32 hi}
constexpr uint8_t kKindSet = 0;  // host -> guests: (a chunk of) the host's whole set, silent
constexpr uint8_t kKindNews = 1; // host -> guests: parts newly unlocked, found by `origin`
constexpr uint8_t kKindFound = 2; // guest -> host: parts this guest has that the host's set lacks
constexpr int kChunk = 100;       // keys per message (8 bytes each: well under one packet)
constexpr int kMaxKeys = 4096;    // a received message with more is refused
constexpr double kPoll = 1.0, kFullEvery = 10.0, kResend = 3.0;

static uintptr_t a_IsUnlocked, a_UnlockPart, a_StrategyGet, a_ExecuteAction;

static uint32_t g_tx_sets, g_tx_news, g_tx_found, g_rx_sets, g_rx_news, g_rx_found, g_applied, g_refused, g_stale;

// host
static std::set<Key> g_announced;              // the set the guests were last sent
static uint32_t g_announced_world = 0;
static double g_last_poll = -100, g_last_full = -100;
static std::map<Key, uint8_t> g_pending;       // guests' finds not applied yet -> who found it
static std::map<Key, uint8_t> g_origin;        // the finder of a key unlocked from g_pending (for the news)

// guest
static std::set<Key> g_host_set;               // the host's set (of g_host_world)
static uint32_t g_host_world = 0;
static bool g_have_set = false;
static double g_last_found_tx = -100;
static std::map<Key, int> g_found_tries;       // how often a key was sent (a key the host refuses stops)
constexpr int kMaxTries = 5;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

// ---------------------------------------------------------------- BUG-0441
// The live Cell -> Creature start (0xE83910) adds 12 creature parts, the creature twins of Cell parts
// (table 0x1485FE8: {cell instance, creature instance} x 12, types 0x40616000 / 0x40626000), to the
// unlockable map (+0x1490, AddUnlockableItem) and unlocks the twins of the Cell parts the cell had
// (state 5). A loaded save rebuilds the map from the game's data without them (312 instead of 324),
// but its item states (+0x4D04) keep them, and IsUnlocked (the palette's check 0x5C6820) reads the state
// first: a guest that loaded the host's world after the evolution offers the same parts in its editor,
// yet the census counted the map only (host parts=55 of 324, guest 49 of 312), and parts.shared never
// unlocked a twin on a guest (its locked list came from the map). Now the census goes over the map and
// the item states together: every game counts what its palette offers.
// `coop.fix parts.census_states off`: the map only (0.7.3.3, BUG-0401's first version).
static bool g_states_fix = true;
COOP_FIX("parts.census_states", "BUG-0441", g_states_fix, "creature: the parts census counts the item states too (a loaded game agrees with the one that came from Cell)");

// the state map (+0x4D04 buckets, +0x4D08 count; node {u32 lo, u32 hi, u8 state (bit 0 unlocked, 4 seen),
// ..., next at +0x10}): every item the game has a state for, also items that are not in the map +0x1490
static bool states(void* items, std::vector<std::pair<Key, uint8_t>>& out)
{
    if (!items || !readable((char*)items + 0x4D04, 8))
        return false;
    void** b = field<void**>(items, 0x4D04);
    uint32_t n = field<uint32_t>(items, 0x4D08);
    for (uint32_t i = 0; b && n < 100000 && i < n && readable(b + i, 4); i++)
        for (uint32_t* nd = (uint32_t*)b[i]; nd && out.size() < 100000; nd = (uint32_t*)nd[4]) {
            if (!readable(nd, 0x14))
                break;
            out.push_back({{nd[0], nd[1]}, (uint8_t)nd[2]});
        }
    return true;
}

bool census(int& unlocked, uint32_t& hash, int& unlockable, std::vector<Key>* locked, std::vector<Key>* unlocked_keys,
            void* items)
{
    unlocked = unlockable = 0;
    hash = 0;
    if (!items)
        items = C.items ? C.items() : nullptr;
    if (!items || !a_IsUnlocked || !readable((char*)items + 0x1490, 8))
        return false;
    std::set<Key> seen;
    auto count = [&](uint32_t lo, uint32_t hi) {
        unlockable++;
        if (thiscall<bool>(a_IsUnlocked, items, lo, hi)) {
            unlocked++;
            uint32_t h = (lo ^ (hi * 0x9E3779B1u)) * 0x85EBCA6Bu;
            hash += h ^ (h >> 13);
            if (unlocked_keys)
                unlocked_keys->push_back({lo, hi});
        } else if (locked)
            locked->push_back({lo, hi});
    };
    void** b = field<void**>(items, 0x1490);
    uint32_t n = field<uint32_t>(items, 0x1494);
    for (uint32_t i = 0; b && n < 100000 && i < n && readable(b + i, 4); i++)
        for (uint32_t* nd = (uint32_t*)b[i]; nd && unlockable < 100000; nd = (uint32_t*)nd[10]) {
            if (!readable(nd, 0x2C))
                break;
            if (g_states_fix)
                seen.insert({nd[0], nd[1]});
            count(nd[0], nd[1]);
        }
    if (g_states_fix) { // the items a live Cell -> Creature start added: in the states, not in the map
        std::vector<std::pair<Key, uint8_t>> st;
        if (states(items, st))
            for (auto& [k, v] : st)
                if (!seen.count(k) && unlockable < 100000)
                    count(k.first, k.second);
    }
    return true;
}

// the keys of `want` that are unlockable here and still locked get unlocked (cost 0); returns them
static std::vector<Key> unlock_missing(const std::set<Key>& want)
{
    std::vector<Key> locked, done;
    int n, all;
    uint32_t h;
    void* items = C.items ? C.items() : nullptr;
    if (!items || !a_UnlockPart || !census(n, h, all, &locked))
        return done;
    for (auto& k : locked)
        if (want.count(k) && thiscall<bool>(a_UnlockPart, items, k.first, k.second, 0)) {
            done.push_back(k);
            g_applied++;
        }
    return done;
}

// ---------------------------------------------------------------- BUG-0467
// The Cell stage keeps its collected parts apart from the Creature's items: 13 slots at the Cell
// player's serializable data S+0x34 + 4*part (S = cCellGame +0x5190; 0 locked, 1 unlocked in a save,
// 2 just collected: 0xE5D7B0 writes 2 when the own cell touches a part token, and S+0x68 / S+0x69 = 1,
// the mating-call button; docs/re/cell.md 12). A part a guest picked up stayed in the guest's game
// ("my guest just took a new piece in cell stage but I don't get it unlocked") and was gone at the
// guest's next load of the host's world. The Cell slots now join the party's set as keys
// {kCellKey, slot}: the host unlocks a guest's slot (2, and the mating-call button, as a pickup does),
// every guest unlocks the host's set. On a join each game's slots are merged too: what either player
// had is everyone's. `coop.fix parts.cell_shared off`: every game keeps its own Cell parts.
static bool g_cell_fix = true;
COOP_FIX("parts.cell_shared", "BUG-0467", g_cell_fix, "cell: the Cell parts a player collects are unlocked for the whole party (host-held union)");
constexpr uint32_t kCellKey = 0xCE11BA57u; // Key.first of a Cell slot (no item key of the game uses it)
constexpr uint32_t kCellSlots = 13;        // S+0x34 .. S+0x64; S+0x68 is the next field

static uint8_t* cell_S()
{
    if (game::mode_id() != modes::kCell)
        return nullptr;
    uint8_t* g = modes::cell_game();
    uint8_t* s = g && readable(g + 0x5190, 4) ? field<uint8_t*>(g, 0x5190) : nullptr;
    return s && readable(s, 0x6C) ? s : nullptr;
}

static bool in_cell() { return g_cell_fix && cell_S() != nullptr; }

// the unlocked Cell slots as keys
static bool cell_census(std::vector<Key>& unlocked)
{
    uint8_t* s = cell_S();
    if (!s)
        return false;
    for (uint32_t i = 0; i < kCellSlots; i++) {
        int32_t v = field<int32_t>(s, 0x34 + 4 * i);
        if (v > 0 && v < 16)
            unlocked.push_back({kCellKey, i});
    }
    return true;
}

static std::vector<Key> cell_unlock_missing(const std::set<Key>& want)
{
    std::vector<Key> done;
    uint8_t* s = cell_S();
    if (!s)
        return done;
    for (auto& k : want) {
        if (k.first != kCellKey || k.second >= kCellSlots)
            continue;
        int32_t& v = field<int32_t>(s, 0x34 + 4 * k.second);
        if (v != 0)
            continue;
        v = 2;                       // as a collected part token
        field<uint8_t>(s, 0x68) = 1; // and its mating-call button: the part can be used in the editor
        field<uint8_t>(s, 0x69) = 1;
        done.push_back(k);
        g_applied++;
    }
    return done;
}

// this stage's unlocked keys (Cell slots or Creature items)
static bool stage_census(std::vector<Key>& mine)
{
    if (in_cell())
        return cell_census(mine);
    int n, all;
    uint32_t h;
    return census(n, h, all, nullptr, &mine);
}

static std::vector<Key> stage_unlock(const std::set<Key>& want)
{
    return in_cell() ? cell_unlock_missing(want) : unlock_missing(want);
}

static void write_keys(net::Writer& w, uint8_t kind, uint8_t origin, uint32_t world, const Key* k, size_t n)
{
    w.u8(kind);
    w.u8(origin);
    w.u32(world);
    w.u16((uint16_t)n);
    for (size_t i = 0; i < n; i++) {
        w.u32(k[i].first);
        w.u32(k[i].second);
    }
}

static void host_send(uint8_t kind, uint8_t origin, const std::vector<Key>& keys)
{
    size_t at = 0;
    do { // an empty set still goes out (the guests learn the host's world)
        net::Writer w;
        size_t n = std::min(keys.size() - at, (size_t)kChunk);
        write_keys(w, kind, origin, g_announced_world, keys.data() + at, n);
        C.sess->broadcast_reliable(net::MSG_PARTS, w);
        (kind == kKindSet ? g_tx_sets : g_tx_news)++;
        at += n;
    } while (at < keys.size());
}

static std::string count_text(size_t n)
{
    const char* what = in_cell() ? "cell part" : "creature part";
    return n == 1 ? std::string("a new ") + what : std::to_string(n) + " new " + what + "s";
}

static uint32_t g_announced_mode = 0; // host: the stage of g_announced (Cell slots and Creature items differ)

static void host_tick(double now)
{
    uint32_t world = C.world ? C.world() : 0;
    if (!world)
        return;
    uint32_t mode = game::mode_id();
    if (world != g_announced_world || mode != g_announced_mode) { // another world or stage: its own set, no news
        g_announced.clear();
        g_origin.clear();
        g_announced_world = world;
        g_announced_mode = mode;
        g_last_full = -100;
    }
    if (!g_pending.empty()) { // the guests' finds
        std::set<Key> want;
        for (auto& [k, o] : g_pending)
            want.insert(k);
        for (auto& k : stage_unlock(want))
            g_origin[k] = g_pending[k];
        g_pending.clear();
    }
    std::vector<Key> mine;
    if (!stage_census(mine))
        return;
    std::set<Key> now_set(mine.begin(), mine.end());
    bool first = g_announced.empty();
    std::map<uint8_t, std::vector<Key>> news; // by finder
    for (auto& k : now_set)
        if (!g_announced.count(k)) {
            auto o = g_origin.find(k);
            news[o == g_origin.end() ? 0 : o->second].push_back(k);
        }
    g_origin.clear();
    bool changed = now_set != g_announced;
    g_announced = std::move(now_set);
    if (!first)
        for (auto& [origin, keys] : news) {
            host_send(kKindNews, origin, keys);
            H->log(M, SK_INFO, "parts: %zu new part(s) found by player %u, %zu in the party's set", keys.size(), origin,
                   g_announced.size());
            if (origin != 0 && C.feed)
                C.feed(C.sess->name_of(origin) + " found " + count_text(keys.size()) + ": it is in your editor too");
        }
    if (changed || now - g_last_full >= kFullEvery) {
        g_last_full = now;
        host_send(kKindSet, 0, std::vector<Key>(g_announced.begin(), g_announced.end()));
    }
}

static void guest_tick(double now)
{
    uint32_t world = C.world ? C.world() : 0;
    if (!g_have_set || !world || world != g_host_world || !C.guest_in_host_world || !C.guest_in_host_world())
        return;
    auto done = stage_unlock(g_host_set); // what the host's world has (also after our own reload)
    if (!done.empty())
        H->log(M, SK_INFO, "parts: %zu part(s) of the party's set unlocked here", done.size());
    std::vector<Key> mine;
    if (!stage_census(mine))
        return;
    std::vector<Key> extra; // found here, not in the host's set yet: sent until the host's set has them
    if (now - g_last_found_tx < kResend)
        return;
    for (auto& k : mine)
        if (!g_host_set.count(k) && g_found_tries[k] < kMaxTries)
            extra.push_back(k);
    if (extra.empty())
        return;
    g_last_found_tx = now;
    for (auto& k : extra)
        g_found_tries[k]++;
    for (size_t at = 0; at < extra.size(); at += kChunk) {
        net::Writer w;
        write_keys(w, kKindFound, C.sess->my_id(), world, extra.data() + at, std::min(extra.size() - at, (size_t)kChunk));
        C.sess->send_reliable_to(0, net::MSG_PARTS, w);
        g_tx_found++;
    }
    H->log(M, SK_INFO, "parts: sent %zu part(s) found here to the host", extra.size());
}

void tick(double now)
{
    if (!g_fix || !C.sess || !a_IsUnlocked || !a_UnlockPart)
        return;
    const bool creature = game::mode_id() == modes::kCreature && game::avatar();
    if ((!creature && !in_cell()) || now - g_last_poll < kPoll) // BUG-0467: the Cell stage's parts too
        return;
    g_last_poll = now;
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_PARTS || !g_fix)
        return;
    uint8_t kind = r.u8(), origin = r.u8();
    uint32_t world = r.u32();
    uint16_t n = r.u16();
    if (!r.ok() || n > kMaxKeys || r.left() < (size_t)n * 8) {
        g_refused++;
        return;
    }
    std::vector<Key> keys(n);
    for (auto& k : keys) {
        k.first = r.u32();
        k.second = r.u32();
    }
    if (is_host() && kind == kKindFound && from != 0) {
        g_rx_found++;
        if (world == 0 || world != (C.world ? C.world() : 0)) { // found in another world than ours
            g_stale++;
            return;
        }
        for (auto& k : keys)
            g_pending.emplace(k, from); // applied by the next poll in Creature
        return;
    }
    if (is_guest() && from == 0 && (kind == kKindSet || kind == kKindNews)) {
        (kind == kKindSet ? g_rx_sets : g_rx_news)++;
        if (world != g_host_world) { // the host plays another world now: its set starts over
            g_host_set.clear();
            g_found_tries.clear();
            g_host_world = world;
        }
        g_have_set = true;
        g_host_set.insert(keys.begin(), keys.end());
        if (kind == kKindNews && origin != C.sess->my_id() && C.feed && !keys.empty())
            C.feed(C.sess->name_of(origin) + " found " + count_text(keys.size()) + ": it is in your editor too");
    }
}

void on_session_end()
{
    g_announced.clear();
    g_announced_world = 0;
    g_pending.clear();
    g_origin.clear();
    g_host_set.clear();
    g_host_world = 0;
    g_have_set = false;
    g_found_tries.clear();
    g_last_full = g_last_poll = g_last_found_tx = -100;
}

int find_part(int level)
{
    void* av = game::avatar();
    if (!av || !a_StrategyGet || !a_ExecuteAction || game::mode_id() != modes::kCreature)
        return -1;
    void* st = cdecl_call<void*>(a_StrategyGet);
    if (!st)
        return -1;
    // as 0xD46BE0 case 6 (a skeleton / bone pile reward) builds it: {creature, 0, level}
    uint32_t msg[3] = {(uint32_t)(uintptr_t)av, 0, (uint32_t)level};
    thiscall<void>(a_ExecuteAction, st, 0x5371F11u, (void*)msg);
    int n, all;
    uint32_t h;
    return census(n, h, all) ? n : -1;
}

std::string dump(const std::string& what)
{
    std::string s;
    char b[64];
    if (what == "keys") { // the unlocked unlockable items (the census)
        std::vector<Key> mine;
        int n, all;
        uint32_t h;
        if (!census(n, h, all, nullptr, &mine))
            return "no collected parts\n";
        std::sort(mine.begin(), mine.end());
        for (auto& k : mine) {
            snprintf(b, sizeof b, "%08X:%08X ", k.first, k.second);
            s += b;
        }
        return s + "\n";
    }
    std::vector<std::pair<Key, uint8_t>> st;
    void* items = C.items ? C.items() : nullptr;
    if (!states(items, st))
        return "no collected parts\n";
    std::sort(st.begin(), st.end());
    snprintf(b, sizeof b, "states=%zu default(+0xC)=%u\n", st.size(), (unsigned)field<uint8_t>(items, 0xC));
    s = b;
    for (auto& [k, v] : st) {
        snprintf(b, sizeof b, "%08X:%08X=%X%s ", k.first, k.second, v,
                 thiscall<bool>(a_IsUnlocked, items, k.first, k.second) ? "u" : "");
        s += b;
    }
    return s + "\n";
}

std::string debug_cell_pick(int slot)
{
    uint8_t* s = cell_S();
    if (!s)
        return "not in the Cell stage";
    if (slot < 0 || slot >= (int)kCellSlots)
        return "slot 0-12";
    int32_t before = field<int32_t>(s, 0x34 + 4 * slot);
    field<int32_t>(s, 0x34 + 4 * slot) = 2;
    field<uint8_t>(s, 0x68) = 1;
    field<uint8_t>(s, 0x69) = 1;
    return "cell slot " + std::to_string(slot) + ": " + std::to_string(before) + " -> 2";
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "shared=%d set=%zu host_set=%zu host_world=%08X have_set=%d pending=%zu tx_sets=%u tx_news=%u tx_found=%u "
             "rx_sets=%u rx_news=%u rx_found=%u applied=%u stale=%u refused=%u",
             g_fix ? 1 : 0, g_announced.size(), g_host_set.size(), g_host_world, g_have_set ? 1 : 0, g_pending.size(),
             g_tx_sets, g_tx_news, g_tx_found, g_rx_sets, g_rx_news, g_rx_found, g_applied, g_stale, g_refused);
    std::string out = b;
    if (uint8_t* s = cell_S()) { // BUG-0467: the Cell slots (0 locked, 1 unlocked, 2 just collected)
        out += "\ncell_shared=" + std::to_string(g_cell_fix ? 1 : 0) + " cell_slots=";
        for (uint32_t i = 0; i < kCellSlots; i++)
            out += std::to_string(field<int32_t>(s, 0x34 + 4 * i));
        out += " mate_button=" + std::to_string(field<uint8_t>(s, 0x69));
    }
    return out;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    a_IsUnlocked = H->sym("Simulator::cCollectableItems::IsUnlocked");
    a_UnlockPart = H->sym("Simulator::cCollectableItems::UnlockPart");
    a_StrategyGet = H->sym("App::cCreatureModeStrategy::Get");
    a_ExecuteAction = H->sym("App::cCreatureModeStrategy::ExecuteAction");
}

} // namespace parts

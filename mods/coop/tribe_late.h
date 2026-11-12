// BUG-9135, roadmap phase 4 (joining late), Tribe: the pure parts of tribe.cpp's late-joiner rules. Portable (no
// Windows, no game): tests/tribe_late_tests.cpp. docs/re/tribe.md#tribe-late-joiners.
//
//  * Decision 4: a partner without a creature of their own (plays one of the host's creatures, or the host's species)
//    gets no village; the host and the partner are told why, the partner's Co-op window asks them to choose their own
//    creature, and a partner who waits for that never holds up the founding of the partners after them.
//  * Decision 5: a partner village founded while the host's village is past the start is caught up to the host's
//    village (members, hut level, tool racks) once, before the founding's checkpoint. Never the host's village, never
//    a village founded before (decision 9).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tribe_late {

// ---- why a connected partner has no village yet (founding_ready) ----
enum class Wait : uint8_t {
    Ready = 0,      // can be founded now
    NoCreatureYet,  // no creature of theirs has reached the host yet, the world is young (it may still come)
    NoCreature,     // still none once the world settled: a late joiner without a creature of their own (a guest who
                    // joins in Tribe and plays the host's species sends none: there is no avatar in Tribe)
    Settling,       // their creature for this world may still change (BUG-9137: 15 s after the world, 10 s after a change)
    HostCreation,   // plays one of the host's own creations (sent without records, BUG-9137)
    FounderSpecies, // plays the species of a founder's village (the host's species, an outfit of it, another partner's)
    StandIn,        // BUG-9150 (owner decision 14, "1B"): one of the three above, founded now with a stand-in of the
                    // host's creature that takes their own creature in place later
};

struct PartnerState {
    bool pending = false;         // a village was made for them and its seat is not saved yet
    bool has_creature = false;    // player_key + species known on the host
    bool settled = false;         // founder_creature_settled (with no creature: the world is 15 s old)
    bool host_creation = false;   // creations::plays_host_creation
    bool founder_species = false; // the creature is a founder seat's species or one of its outfits
};

// `settle_fix`: tribe.founder_settle (BUG-9137; off: neither the wait nor the host-creation check).
// `skip_fix`: tribe.founding_skip (BUG-9138; off: a partner on a founder's species counts as ready and then stops the
// founding with a message or silently, for every partner after them too).
// `stand_in`: tribe.stand_in (BUG-9150): a partner without a creature of their own (no creature once settled, one of
// the host's creations, a founder's species) is founded at once with a stand-in instead of waiting.
inline Wait founding_wait(const PartnerState& p, bool settle_fix, bool skip_fix, bool stand_in = false)
{
    if (p.pending) return Wait::Ready; // the village is made: finish saving its seat
    if (!p.has_creature) return p.settled ? (stand_in ? Wait::StandIn : Wait::NoCreature) : Wait::NoCreatureYet;
    if (settle_fix) {
        if (!p.settled) return Wait::Settling;
        if (p.host_creation) return stand_in ? Wait::StandIn : Wait::HostCreation;
    }
    if (skip_fix && p.founder_species) return stand_in ? Wait::StandIn : Wait::FounderSpecies;
    return Wait::Ready;
}

// a partner founded now: with their own creature (Ready) or with a stand-in (StandIn)
inline bool founds_now(Wait w) { return w == Wait::Ready || w == Wait::StandIn; }

// a wait worth telling the host and the partner (the others are seconds long)
inline bool told(Wait w) { return w == Wait::NoCreature || w == Wait::HostCreation || w == Wait::FounderSpecies; }

// the partner found_partners founds now: the first ready one in join order, -1 none. `why`: the first wait worth
// telling (index into `waits`, -1 none), reported while nobody is founded.
inline int pick_partner(const std::vector<Wait>& waits, int* why = nullptr)
{
    if (why) *why = -1;
    for (size_t i = 0; i < waits.size(); ++i) {
        if (founds_now(waits[i])) return (int)i;
        if (why && *why < 0 && told(waits[i])) *why = (int)i;
    }
    return -1;
}

// what everybody reads (MSG_NOTICE goes to every player, so the words fit the host and the partner alike)
inline std::string wait_text(Wait w, const std::string& name)
{
    switch (w) {
    case Wait::NoCreature:
        return name + " has no creature of their own here, so they have no village yet. They can choose their own creature "
                      "in the Co-op window (F9).";
    case Wait::HostCreation:
    case Wait::FounderSpecies:
        return name + " plays one of the host's creatures, so they have no village of their own. They can choose their own "
                      "creature in the Co-op window (F9).";
    default:
        return {};
    }
}

// the host's Co-op window status line while a partner waits (short: one line of the window)
inline std::string host_status(Wait w, const std::string& name)
{
    if (!told(w)) return {};
    return name + (w == Wait::NoCreature ? " has no village yet: they have no creature of their own."
                                         : " has no village yet: they play one of your creatures.");
}

// ---- the partner's Co-op window (guest) ----
constexpr double kPromptSettle = 15; // seconds after the Tribe world was entered (the host's founding waits as long)

enum class Prompt : uint8_t {
    None = 0,       // has a village, not a guest in Tribe, or the world has not settled: the window as before
    ChooseCreature, // no creature of their own: "Choose your own creature for this game" with the creature selector
    VillageComing,  // their own creature was chosen, the host founds the village: the selector stays for a change
    StandInChoose,  // BUG-9150: their village wears a stand-in of the host's creature: choose or make their own
    StandInComing,  // BUG-9150: their own creature was chosen: the village takes it shortly
};

// a guest in the host's Tribe world without a village of their own (or with one that still wears its stand-in), once
// the world settled
inline Prompt prompt(bool guest_in_tribe, bool has_village, bool plays_own, double seconds_in_world, bool stand_in_village = false)
{
    if (!guest_in_tribe || seconds_in_world < kPromptSettle) return Prompt::None;
    if (has_village) return !stand_in_village ? Prompt::None : plays_own ? Prompt::StandInComing : Prompt::StandInChoose;
    return plays_own ? Prompt::VillageComing : Prompt::ChooseCreature;
}

// ---- catch-up of a new partner village (decision 5) ----
constexpr int kMaxLevel = 4;              // Community_SetLevel's range as shared_hut_upgrade uses it
constexpr uint16_t kRackKinds = 0x3FE;    // tool kinds 1..9 (racks; 10 is the fire pit every village has)
constexpr uint32_t kMaxCatchUpMembers = 40; // as `coop.tribe spawn`; the game's population limit applies too

struct Village {
    int level = 0;          // community level (+0x38)
    uint32_t adults = 0;    // living members by age (+0xB34: 1 adult, 0 baby)
    uint32_t babies = 0;
    uint16_t tools = 0;     // bit k: a rack of kind k
};

struct CatchUp {
    int level = -1;         // the hut level to set, -1: keep
    uint32_t adults = 0;    // members to add
    uint32_t babies = 0;
    uint16_t tools = 0;     // rack kinds to add
    bool any() const { return level >= 0 || adults || babies || tools; }
};

// What the fresh village needs to be like the host's: the host's hut level, the host's member count (adults first,
// up to the host's adults, the rest babies; members are never removed), the host's racks it has not got.
inline CatchUp catch_up_plan(const Village& host, const Village& fresh)
{
    CatchUp c;
    if (host.level >= 0 && host.level <= kMaxLevel && host.level != fresh.level) c.level = host.level;
    const uint32_t want = host.adults + host.babies, have = fresh.adults + fresh.babies;
    if (want > have) {
        const uint32_t need = std::min(want - have, kMaxCatchUpMembers);
        c.adults = std::min(need, host.adults > fresh.adults ? host.adults - fresh.adults : 0u);
        c.babies = need - c.adults;
    }
    c.tools = (uint16_t)(host.tools & ~fresh.tools & kRackKinds);
    return c;
}

// the rack kinds of a mask, ascending
inline std::vector<int> kinds(uint16_t mask)
{
    std::vector<int> out;
    for (int k = 1; k <= 9; ++k)
        if (mask & (1u << k)) out.push_back(k);
    return out;
}

// the log line of one catch-up (what was planned, what the game did)
inline std::string describe(const CatchUp& plan, int level_set, uint32_t adults_made, uint32_t babies_made, uint16_t tools_made)
{
    std::string s = "hut level ";
    s += plan.level < 0 ? std::string("kept") : level_set >= 0 ? std::to_string(level_set) : std::string("not set");
    s += ", members +" + std::to_string(adults_made + babies_made) + " of " + std::to_string(plan.adults + plan.babies);
    s += " (" + std::to_string(adults_made) + " adults, " + std::to_string(babies_made) + " babies), racks";
    const auto want = kinds(plan.tools);
    if (want.empty()) s += " none";
    for (int k : want) s += std::string(" ") + std::to_string(k) + ((tools_made & (1u << k)) ? "" : "(not placed)");
    return s;
}

} // namespace tribe_late

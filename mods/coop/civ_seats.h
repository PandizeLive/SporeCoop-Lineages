// BUG-9135 (roadmap phase 5): the Civilization founder nations of up to founders::kPlaySeats players, one per seat.
//
// Pure rules (no game state; tests/civ_seats_tests.cpp): which seat a guest plays, who owns a seat's cities and
// vehicles, which seat gets the next capital, when a new capital may be founded and which nation is the new one.
// civ.cpp (switch civ.multi_founders) feeds them the founder roster (founders::Roster, Stage::Civilization), the
// Tribe roster, the outfit histories and the live cities. Seat 0 is the host's nation, seats 1.. the partners'.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "founders.h"

namespace civ_seats {

constexpr uint8_t kNobody = 0xFF;    // party::kNobody: no connected player in a seat
constexpr uint8_t kNoOwner = 0xFE;   // civ.cpp: a founder nation nobody may command now (its player is away)
constexpr uint8_t kOtherSeat = 0xFC; // guest: another partner's nation (only the host knows that player's id)

// the seats the gameplay uses: the founded seats, at most founders::kPlaySeats (owner decision 10)
inline size_t play_seats(const founders::Roster& r) { return std::min(r.seats(), founders::kPlaySeats); }

// The seat's species: the nation's recorded species or any look of the seat's tribal founder (the outfit history).
// Outfits::contains is the same test; it lives in founders.cpp, this header stays free of it.
inline bool seat_species(const founders::Roster& r, const founders::Outfits& outfits, int seat, uint32_t species)
{
    if (!species || seat < 0 || seat >= (int)founders::kMaxSeats)
        return false;
    if (r.entries[seat].species == species)
        return true;
    const auto& history = outfits.species[seat];
    return std::find(history.begin(), history.end(), species) != history.end();
}

// Before BUG-9135 every partner commanded the second nation. That rule stays for a two-founder roster whose partner
// seat has no identity yet, while exactly one partner is in the session.
inline bool sole_partner_seat(const founders::Roster& r, int seat, size_t partners_present)
{
    return seat == 1 && r.seats() == 2 && !r.entries[1].player && partners_present == 1;
}

// The seat a guest plays: its identity, else its species on a seat without identity, else the two-founder rule.
// -1: none (not a founder of this campaign, or a seat the guest cannot recognise before the host shares the world).
inline int own_seat(const founders::Roster& r, const founders::Outfits& outfits, uint64_t identity, uint32_t species,
                    size_t partners_present)
{
    const int n = (int)play_seats(r);
    if (identity)
        for (int s = 1; s < n; ++s)
            if (r.entries[s].player == identity)
                return s;
    for (int s = 1; s < n; ++s)
        if (!r.entries[s].player && seat_species(r, outfits, s, species))
            return s;
    return sole_partner_seat(r, 1, partners_present) ? 1 : -1;
}

// Who owns a founder seat's cities and vehicles (the IsPlayerOwned hooks and the host's owner tables).
//   host: the connected player in the seat (seat_player, kNobody = away); an away seat is the host's while offline
//         control is on, else nobody's (kNoOwner)
//   guest: its own seat is its own (me), every other partner seat kOtherSeat
inline uint8_t owner_of_seat(int seat, bool host, uint8_t seat_player, int local_seat, uint8_t me, bool offline_control)
{
    if (seat == 0)
        return 0;
    if (seat < 0)
        return kNobody;
    if (!host)
        return seat == local_seat ? me : kOtherSeat;
    if (seat_player != kNobody)
        return seat_player;
    return offline_control ? 0 : kNoOwner;
}

// The partner seat whose capital comes next: seats are founded in order (a roster has no gaps), each from the Tribe
// roster's seat of the same index (its nation's species descends from that tribal founder, founders::can_record),
// at most founders::kPlaySeats. civ_seats = the Civilization roster's seats (0 = none yet). -1 = nothing to found.
inline int next_capital_seat(size_t civ_seats, size_t tribal_seats)
{
    const size_t wanted = std::min(tribal_seats, founders::kPlaySeats);
    const size_t next = civ_seats ? civ_seats : 1;
    return next < wanted ? (int)next : -1;
}

struct City {
    uint32_t faction = 0; // the city's political id
    bool home = false;    // the city's nation is the native player country (the host's)
    bool blank = false;   // neither the city nor its nation has a species key yet (a fresh native city)
    bool nation = true;   // the city has a nation (one without is no new capital; it still blocks a founding)
};

// A new partner capital is founded only while every city belongs to the host's nation or a founder nation: an
// established NPC nation is never repurposed, and the new nation is told apart from the native ones by being the only
// one left (the rule of the two-founder campaign: "only from the native graduation's single capital").
inline bool only_founder_cities(const std::vector<City>& cities, const founders::Roster& r)
{
    if (cities.empty())
        return false;
    bool home = false;
    for (const auto& c : cities) {
        if (c.home)
            home = true;
        else if (!r.contains_faction(c.faction))
            return false;
    }
    return home;
}

// After the founding: the new nation's political id once its capital appeared. 0 = not yet (the appearance is
// asynchronous); kAmbiguous = another nation or a species key is there too (the identity is retained, nothing bound).
constexpr uint32_t kAmbiguous = 0xFFFFFFFFu;
inline uint32_t new_capital(const std::vector<City>& cities, const founders::Roster& r)
{
    uint32_t found = 0;
    for (const auto& c : cities) {
        if (!c.nation || c.home || r.contains_faction(c.faction))
            continue;
        if (found || !c.blank || !c.faction)
            return kAmbiguous;
        found = c.faction;
    }
    return found;
}

// Identities to learn: a connected partner (its identity, its creature species) whose identity is not seated yet
// belongs to the seat without identity whose species it plays, when that partner matches exactly one such seat and
// no other unseated partner matches that seat too. Returns {seat, index into partners}, each seat at most once.
struct Partner {
    uint64_t identity = 0;
    uint32_t species = 0;
};
inline std::vector<std::pair<int, size_t>> identities_to_bind(const founders::Roster& r, const founders::Outfits& outfits,
                                                              const std::vector<Partner>& partners)
{
    const int n = (int)r.seats();
    auto unseated = [&](const Partner& who) { return who.identity && r.seat_of_player(who.identity) < 0; };
    auto matches = [&](const Partner& who, int s) {
        return unseated(who) && !r.entries[s].player && seat_species(r, outfits, s, who.species);
    };
    std::vector<std::pair<int, size_t>> out;
    for (int s = 1; s < n; ++s) {
        size_t found = 0, count = 0;
        for (size_t p = 0; p < partners.size(); ++p)
            if (matches(partners[p], s)) {
                found = p;
                ++count;
            }
        if (count != 1)
            continue;
        int seats_matched = 0;
        for (int t = 1; t < n; ++t)
            seats_matched += matches(partners[found], t);
        bool twice = false; // the same identity twice in the session (a stale entry): bind nothing
        for (size_t p = 0; p < partners.size(); ++p)
            twice |= p != found && partners[p].identity == partners[found].identity;
        if (seats_matched == 1 && !twice)
            out.push_back({s, found});
    }
    return out;
}

} // namespace civ_seats

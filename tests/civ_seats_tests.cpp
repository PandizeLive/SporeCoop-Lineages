// BUG-9135: the Civilization founder seat rules of up to four players (mods/coop/civ_seats.h)
#include <cstdio>
#include <vector>

#include "civ_seats.h"

static int failures;
static void check(bool ok, const char* name)
{
    if (!ok) {
        ++failures;
        printf("FAIL %s\n", name);
    }
}

using founders::Roster;
using namespace civ_seats;

static Roster roster(std::vector<founders::Founder> seats)
{
    Roster r;
    for (size_t i = 0; i < seats.size(); ++i)
        r.entries[i] = seats[i];
    return r;
}

int main()
{
    // a two-founder campaign as before BUG-9135 (CIV1: no identities)
    const Roster two{0x100, 0x200, 0x21CA, 0x21CB};
    // four founders, identities known for seats 1 and 3
    const Roster four = roster({{0x100, 0x21CA, 0xA0}, {0x200, 0x21CB, 0xA1}, {0x300, 0x21CC, 0}, {0x400, 0x21CD, 0xA3}});
    founders::Outfits outfits;
    outfits.species[2] = {0x2F0, 0x300}; // seat 2's tribal founder changed its look once

    check(play_seats(two) == 2 && play_seats(four) == 4, "play seats");
    Roster five = four;
    five.entries[4] = {0x500, 0x21CE, 0xA4};
    check(play_seats(five) == founders::kPlaySeats, "play seats capped at kPlaySeats");

    // seat_species: the nation's species or any look of the seat
    check(seat_species(four, outfits, 2, 0x300) && seat_species(four, outfits, 2, 0x2F0), "seat species, history");
    check(!seat_species(four, outfits, 1, 0x2F0) && !seat_species(four, outfits, 2, 0), "seat species, other seat / none");
    check(!seat_species(four, outfits, 9, 0x300) && !seat_species(four, outfits, -1, 0x300), "seat species, bad seat");

    // the two-founder rule: one partner in the session, a two-seat roster without identity
    check(sole_partner_seat(two, 1, 1), "sole partner commands seat 1");
    check(!sole_partner_seat(two, 1, 2), "two partners: no sole partner");
    check(!sole_partner_seat(four, 1, 1), "four seats: no sole partner rule");
    Roster bound = two;
    bound.entries[1].player = 0xB1;
    check(!sole_partner_seat(bound, 1, 1), "an identity on seat 1: no sole partner rule");

    // own_seat (guest)
    check(own_seat(two, {}, 0, 0, 1) == 1, "two founders, one partner: seat 1 as before");
    check(own_seat(two, {}, 0, 0x999, 2) == -1, "two founders, a third player with another species: no seat");
    check(own_seat(two, {}, 0, 0x200, 2) == 1, "two founders, three players: the species tells");
    check(own_seat(four, outfits, 0xA3, 0x999, 3) == 3, "identity first");
    check(own_seat(four, outfits, 0xA3, 0x300, 3) == 3, "identity before species");
    check(own_seat(four, outfits, 0x77, 0x2F0, 3) == 2, "species of a seat without identity (earlier look)");
    check(own_seat(four, outfits, 0x77, 0x200, 3) == -1, "a seat with another identity is not taken by species");
    check(own_seat(bound, {}, 0x77, 0x999, 1) == -1, "a bound partner seat is not taken by the sole partner rule");
    check(own_seat(four, outfits, 0xA0, 0x100, 3) == -1, "the host's seat is never a guest's");
    check(own_seat(five, outfits, 0xA4, 0x500, 4) == -1, "seats past kPlaySeats are not played");

    // owner_of_seat
    check(owner_of_seat(0, true, 0, -1, 0, false) == 0 && owner_of_seat(0, false, kNobody, 2, 2, false) == 0, "seat 0 is the host's");
    check(owner_of_seat(2, true, 3, -1, 0, false) == 3, "host: the connected player of the seat");
    check(owner_of_seat(2, true, kNobody, -1, 0, false) == kNoOwner, "host: an away seat has no owner");
    check(owner_of_seat(2, true, kNobody, -1, 0, true) == 0, "host: an away seat is the host's with offline control");
    check(owner_of_seat(2, false, kNobody, 2, 5, false) == 5, "guest: own seat");
    check(owner_of_seat(1, false, kNobody, 2, 5, false) == kOtherSeat, "guest: another partner's seat");
    check(owner_of_seat(1, false, kNobody, -1, 5, true) == kOtherSeat, "guest without a seat: never its own");
    check(owner_of_seat(-1, true, 1, -1, 0, true) == kNobody, "no seat");
    check(kOtherSeat != kNoOwner && kOtherSeat != kNobody && kOtherSeat >= 8, "markers are no player ids");

    // next_capital_seat: in seat order, from the Tribe roster's seats, capped
    check(next_capital_seat(0, 2) == 1, "two founders: the partner's capital first");
    check(next_capital_seat(2, 2) == -1, "two founders: nothing more");
    check(next_capital_seat(0, 0) == -1 && next_capital_seat(0, 1) == -1, "no tribal partner: nothing");
    check(next_capital_seat(2, 4) == 2 && next_capital_seat(3, 4) == 3 && next_capital_seat(4, 4) == -1, "four: one at a time");
    check(next_capital_seat(4, 6) == -1 && next_capital_seat(3, 8) == 3, "capped at kPlaySeats");

    // only_founder_cities and new_capital
    const Roster none;
    std::vector<City> graduation{{0x21CA, true, false}};
    check(only_founder_cities(graduation, none), "the native graduation's single capital");
    check(new_capital(graduation, none) == 0, "nothing new yet");
    std::vector<City> appeared{{0x21CA, true, false}, {0x21CB, false, true}};
    check(new_capital(appeared, none) == 0x21CB, "the new blank nation");
    check(!only_founder_cities(appeared, none), "an unseated nation blocks the next founding");
    check(only_founder_cities(appeared, two), "home and seated founders only");
    std::vector<City> keyed{{0x21CA, true, false}, {0x21CB, false, false}};
    check(new_capital(keyed, none) == kAmbiguous, "a nation with a species is not taken");
    std::vector<City> both{{0x21CA, true, false}, {0x21CB, false, true}, {0x21CC, false, true}};
    check(new_capital(both, none) == kAmbiguous, "two new nations: ambiguous");
    check(new_capital(both, two) == 0x21CC, "the seated nation is skipped");
    std::vector<City> npc{{0x21CA, true, false}, {0x21CB, false, false}, {0x21D0, false, false}};
    check(!only_founder_cities(npc, two), "an NPC nation: no founding");
    check(!only_founder_cities({}, two) && !only_founder_cities({{0x21CB, false, false}}, two), "no home city: no founding");

    // identities_to_bind
    Roster legacy4 = four;
    legacy4.entries[1].player = 0;
    std::vector<Partner> partners{{0xB1, 0x200}, {0xB2, 0x2F0}, {0xA3, 0x400}, {0, 0x300}};
    auto bind = identities_to_bind(legacy4, outfits, partners);
    check(bind.size() == 2 && bind[0] == std::make_pair(1, size_t(0)) && bind[1] == std::make_pair(2, size_t(1)),
          "seats 1 and 2 learn their partners; a seated identity and a partner without one are skipped");
    std::vector<Partner> twins{{0xB1, 0x200}, {0xB5, 0x200}};
    check(identities_to_bind(legacy4, outfits, twins).empty(), "two partners of the same species: nothing bound");
    std::vector<Partner> stale{{0xB1, 0x200}, {0xB1, 0x999}};
    check(identities_to_bind(legacy4, outfits, stale).empty(), "the same identity twice: nothing bound");
    check(identities_to_bind(four, outfits, {{0xA1, 0x300}}).empty(), "an identity already seated is not moved");

    printf("civ seats: %d failures\n", failures);
    return failures ? 1 : 0;
}

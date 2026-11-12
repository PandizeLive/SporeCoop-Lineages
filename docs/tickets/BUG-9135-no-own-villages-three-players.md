---
id: BUG-9135
title: Tribe / Civ: with three or more players no partner gets a village of their own
status: open
kind: feature
stage: Tribe Civ
reported: 2026-11-11
source: owner report on 0.7.8-lineages53-dev ("Started the save, but they still don't have their own tribes when launching")
role: host
versions: 0.7.8-lineages46-dev to 0.7.8-lineages53-dev (by design since the founders were added)
fixed_in:
branch: feat/lineages-47
commits:
tests: tests/civ_seats_tests.cpp (unit, Civ seat rules)
signature: host log has no "tribe automatic founders" line at all; session has 3 players (host + 2 partners)
docs: docs/re/civ.md#civ-multi-founders
related: BUG-9040 BUG-9134 BUG-9054 BUG-9056 BUG-9073
switch: tribe.auto_founders, civ.multi_founders
---

## Report
The owner hosted "Jiff Peanutbutter.spo" (Tribe) with two partners (Kittykath and BigTexas, players 1 and 2). Neither
partner got a village of their own. The host log has no founding line, not even a refusal.

## Cause
The automatic founders (tribe.cpp auto_founders_tick) only run with exactly two players
(`C.sess->players().size() != 2` returns before anything is checked or logged). The whole founder design is for one
partner: founders::Roster saves slots 0 and 1 only (founders.cpp refuses a roster with slots 2 or 3 filled), Outfits
has two seats, and about 40 places in tribe.cpp use `g_order_roster.guest()` (orders, alliances, damage, rescue,
performance, the village commander). Several Tribe features also check for exactly two players (tribe.cpp 2010, 2078,
4608, 4631, 4790, 4907; civ.cpp 5700). Civ founding has the same two-player limit.

## Needed
One founder seat per partner (up to four seats, as the campaign seat rules already allow): the roster's save format,
founding a village for each partner without one, every `guest()` use mapped from the partner's player id to their seat,
the village commander per partner, and the same in Civilization. Until then the session should at least say so instead
of doing nothing.

## Workaround
Play Tribe and Civ with two players (the host and one partner): the partner's village is founded within a few seconds of
arriving.

## Phase 1 done (2026-11-11, feat/lineages-47): identity and the N-seat formats
* identity.{h,cpp}: one random 64-bit identity per Spore profile (SporeCoop\identity.bin next to EditorSaves.package);
  a guest sends it to the host after every join (MSG_IDENTITY 83, u64); `coop.identity`. Two PCs (TITAN host + QA
  guest + MSI): the host knew p1 and p2; after the MSI left and rejoined it was network player 1 instead of 2 and the
  host still mapped it to the MSI's identity.
* founders: kMaxSeats 8; Founder.player (identity); DUO2 / CIV2 (u8 seats, per seat species, faction, u64 player)
  written only for 3+ seats or a known identity (DUO1 / CIV1 byte for byte as before otherwise); a roster may grow
  (seats added, an identity learnt once) but never lose or change a seat; OFT2 for the outfit histories of 3+ seats
  (an OFT1 file still reads after seats were added). tests/win_tests.cpp test_founders_multi.
* Gameplay unchanged: tribe.cpp / civ.cpp still found and bind seats 0 and 1 only.

## Remaining phases (each its own session, tested with 3-4 games: TITAN QA instances + the MSI)
1. Tribe binding: seat_of_player (identity first, the creature match for a seat without one), player_of_seat,
   founder_village(seat); village_commander -> per village; host_command checks the sender's seat; record the identity
   of a seat on its first match.
2. Tribe founding: auto_founders_tick founds one village per unseated partner (one per tick, per-seat pending state,
   next free site); the BUG-9137 settle / host-creation guards per partner.
3. Tribe rules that count founders: h_NounData, pace_holds, missing_last_objective (6/7), six rivals (8),
   h_FactionFlag, village_allied_with_founder, h_TribePlayerOwned / h_MaxPopulation / h_BTTick, shared_hut_upgrade.
4. Tribe state on the wire: MSG_TRIBE_STATE food entries and the rescue trailer for N seats (a new tag), the guest's
   food line, g_founder_rescues / g_remote_rescues per seat, protect / rebuild / capture with an explicit seat,
   CMD_REBUILD / CMD_GIVE_UP with a seat, the outfit vote per seat, performances per performer seat.
5. Civilization: founder_owner per seat, founder_tick (a capital per partner), purchases / cursor / offline control /
   campaign_tick / hud progress for every partner nation (with the Civ items BUG-9054 partner nation AI and budget, BUG-9056
   shared progression, BUG-9073 turret purchases).
6. Space: begin_personal_launch takes the joining player's seat; MSG_SPACE_LAUNCH for N seats.
7. More than four: world.cpp's lobby check `p.id >= seats`, FST7 seats (1..4), the campaign seat UI; kMaxSeats 8.

## Design direction (owner, 2026-11-11)
* Partners' villages and cities are ADDED next to the native ones (one per player), never replacing a native tribe.
* They must come from the game's own creation and upgrade logic, per player, or co-op loses progression: the nest
  becoming the village at the Tribe start, the tribe becoming the city, upgrades on the player's own village. Today the
  founders spawn a fresh native NPC tribe (SpawnNpcTribe: 3 members, 15 food) 100-180 m from the host's village and
  take over a native "next city" (cCivExpansion::SpawnNextCity) in Civ; the game's progression sees only the host's
  tribe / nation and the mod copies progress over (shared hut upgrades, merged tech, BUG-9056 open).
* First research step: how the native Creature -> Tribe and Tribe -> Civ transitions create the player's tribe / first
  city (from the nest, from the tribe), and whether each partner's own game can treat its own village as the native
  player tribe (cGameNounManager GetPlayerTribe / SetPlayerTribe, one per game) while the host stays the world's
  authority. That per-player perspective also suits a later open-world fork (independent players and packs).

## Decisions (2026-11-12)
The owner's confirmed decisions for 3-4 players with own nests, and the proposed foundation phase, are in
docs/ROADMAP.md (the overview kept up to date); this ticket keeps the code-level details.

## Phase 5 (Civilization) in code (2026-11-12, feat/lineages-47; not yet run in a game)
Switch `civ.multi_founders` (on; off = the two nations of before, byte for byte the old code path). Details and the
rules: docs/re/civ.md#civ-multi-founders; pure rules mods/coop/civ_seats.h, unit test tests/civ_seats_tests.cpp
(fails when the two-founder rule ignores the identity or the number of partners).
* founder_owner per seat: host `party::founder_player_of` with the species match (nation species or the seat's outfit
  history) and the two-founder rule (one partner, a two-seat roster without identity: that partner, as before); a
  guest its own seat (`civ_seats::own_seat`: identity in its copy of the roster, species, the two-founder rule).
  Feeds h_CityOwned, h_VehicleOwned, host_assign_cities, the vehicle owners of host_tick and the purchase check.
* A seat without identity learns it from the partner who alone plays its species (roster written once, CIV2).
* founder_tick: one capital per Tribe roster partner seat, one at a time in seat order (per-seat spawned bits), only
  while every city is the host's or a seated founder's; the new nation is the only blank unknown one; the roster is
  written after each, the world saved or shared after the last. Recovery recreates missing saved capitals one at a time.
* Per seat: the guest's purchase dialogue and terrain cursor, CMD_CITY_PURCHASE (the sender's seat), the host's
  offline selection (every absent seat), campaign_tick (tech, highest count, unlocks over every founder nation),
  h_HudProgress (every partner nation's cities), the founding AI hold, `coop.civ bind_guest <nation> [player]`.
* No wire format change. The guest learns its nation from the roster file in the shared save, as before.

Still two nations or open:
* A Civ seat needs a Tribe roster seat (founders::can_record): with today's Tribe code (seats 0 and 1 only) a third
  player gets no capital until phase 3 founds their village, or a late-joiner rule (phase 4) lets founders.cpp
  record a Civ seat without a tribal one.
* Founding is refused once an NPC nation exists ("not reserved before other nations appeared"): a partner seat
  added to an advanced Civ save needs phase 4's catch-up capital.
* The capital takes the next free native site, not the one nearest the partner's former village: the site record's
  position field is unknown and a guest replays "the next site"; `coop.civ sites` dumps the records and the
  founding logs the capital's distance to the former village (docs/re/civ.md#civ-multi-founders).
* `.duo-command` (CMD1) binds the home and seat 1 factions only; its flag covers every absent seat. A per-seat binding
  needs a CMD2 format in founders.cpp.
* party seats (`.duoparty`, join order) and founder roster seats (Tribe order) are separate lists; civ uses the
  founder roster's seats only.
* Not touched: BUG-9054 (shared budget), BUG-9056 (history, Space readiness), BUG-9073 (turret planner) beyond the
  per-nation generalisation above.

To verify in a live three-player Civilization game (host + 2 partners, a Tribe save whose roster has three seats):
1. Host log: `civ automatic founders: Native partner capital founding requested.`, then seat 1 bound, then
   `Native capital founding requested for partner seat 2.` and `Partner seat 2 now has a native Civilization
   capital.`; `seat N capital ... from its former village` lines; one world share after the last capital.
2. `coop.civ nations` on all three games: three founder nations with the three species; `coop.civ` status
   `founders:` line: host seats 1 and 2 with their players, each guest `local_seat` 1 or 2 (its own).
3. Each partner selects and moves only its own nation's vehicles, opens only its own cities' menus and buys there
   (city menu purchase debits the shared treasury); the other partner's vehicles cannot be selected.
4. A city purchase (economic) by partner 2 is made by nation 2; a capture by partner 2 raises the home campaign
   counter (`coop.civ campaign`), tech and unlocks match on all three nations, the HUD bar counts all cities.
5. Partner 2 leaves: its nation's vehicles have no owner (0xFE) unless offline control is on (then the host selects
   them, also while partner 1 stays); it rejoins under another network id: it commands nation 2 again (identity).
6. Save, quit, reload: the capitals keep their species, nobody's nation is founded again, owners return.
7. With `coop.fix civ.multi_founders off` and two players: the old behaviour (lab duo_civ_* tests).

## Roadmap phase 3, Tribe parts (2026-11-12, feat/lineages-47): built, not yet run in a game
Remaining phases 1-4 above (Tribe binding, founding, rules, state on the wire) in tribe.cpp behind `tribe.multi_founders`
(on; off = the two-founder co-op exactly as before). Details: docs/re/tribe.md#tribe-multi-founders. Pure parts
(counts, the TRRS rescue trailer, the command seat word, the founding hold for older seats, the HUD food line) in
tribe_seats.h with tests/tribe_seats_tests.cpp (host unit test, in `make test`).
* Seat binding through party::founder_seat_of / founder_player_of (identity, else the creature match); a seat without
  identity learns its partner's identity once (founders::record, DUO1 becomes DUO2); `coop.tribe bind_founder`.
* Founding: one village per unseated partner, one a tick, the partner's own Creature nest as the site when usable,
  else the next free site; BUG-9137 guards per partner; one checkpoint after the last.
* Orders, ownership, food, purchases, performances, outfit votes, rescue / capture / rebuild per seat; every rule that
  counted two founders counts the founder seats (h_NounData, pace_holds, missing_last_objective 4 + seats, backfill
  5 + seats, six-rival trial 6 + seats, h_FactionFlag, village_allied_with_founder, shared_hut_upgrade, rewind hold).
* Wire: two seats byte for byte as before; 3+ seats: a food entry per founder village, rescue trailer TRRS
  (n x {u16 member, u16 tenths}, u8 n, u32 tag); CMD_REBUILD / CMD_GIVE_UP: seat + 1 in the action word (0: not given).
* Not changed (fixtures with exactly one guest): `coop.tribe register_guest / found_guest / refound_guest`.

To verify live (host + two partners, three games; then once with the switch off):
1. Fresh Creature -> Tribe with three players (each with an own creature and nest): both partners get a village
   (log `tribe automatic founders: seat 1 / seat 2 ...`, `at their own nest` or `at the next free site`), one
   checkpoint reload; `coop.tribe` on the host: `seats=3`, both partner seats with an identity and a commander.
2. Each partner selects and orders only their own village's members (walk, gather, raid, equip, build, baby, perform);
   the other partner's members are refused (`coop.tribe` rejected count on the host).
3. Food line on each guest (`Host X   You Y`) shows its own village; the host's line lists both partners.
4. Down the last villager of partner 2's village: protected; partner 1's or the host's villager revives it; a guest's
   hold-G gives up only its own; capture and `coop.tribe rebuild` from another founder rebuild that seat.
5. Tribal outfit vote (all three edit): each village gets its own partner's outfit, one checkpoint.
6. Rejoin of a partner (another network id): the same seat by identity; a third partner joining later gets the next
   seat; with an older save (seat 1 without identity) the seat-1 partner is recognised by creature and bound.
7. Campaign: a rival appears with three founders, goals count founders + NPCs (`coop.tribe npc_budget`), hut upgrades
   reach both partner villages.

## Phase 3 live test (2026-11-12, feat/lineages-47)
Three games on two PCs (QA host + QA partner on TITAN at 5 FPS, the MSI as the third), a fresh copy of the owner's
Jiff Peanutbutter save (seat 1 founded with the host's creature, BUG-9137). Passed: checks 1 (on an existing Tribe
save, not a fresh Creature start), 2 (orders of each partner reach only its own village: 5 done, 0 rejected), 3 (food
lines), 6 (a rejoin keeps the seat; a later partner gets the next seat; the older seat is claimed and bound). Fixed on
the way: the free-site search found no spot for seat 2 (rings around the host's village only); it now also searches
around the other founder villages (docs/re/tribe.md#tribe-multi-founders). Not yet live: checks 4, 5, 7, 8 and a fresh
Creature -> Tribe start (needs the host's clicks), and all of phase 5 (Civilization).

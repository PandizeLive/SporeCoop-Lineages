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
tests: -
signature: host log has no "tribe automatic founders" line at all; session has 3 players (host + 2 partners)
docs: -
related: BUG-9040 BUG-9134
switch: tribe.auto_founders
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

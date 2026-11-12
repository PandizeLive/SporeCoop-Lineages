# SporeCoop Lineages roadmap

The owner's decisions and the plan that follows from them. Updated whenever a decision is made or a phase moves.
Details of each piece of work live in its ticket (docs/tickets/); this file is the overview. Dates are when a
decision was made.

## Long-term goal (not committed)

An open-world MMO fork of the co-op mod (owner, 2026-11-11): every player plays on their own planet, a planet holds
roughly 10-20 players, players can spawn on almost any habitable planet; fully independent players and packs; a
party system for real players; difficulty per party; repopulation of extinct species after a while.

Design rules that follow from it, for the co-op work now:

* A player is a persistent identity, never a network slot (identity.cpp, BUG-9135).
* Every player owns their own things: species, nest, pack, village, city. The host is the shared world's authority,
  not "the" player.
* Things are created and upgraded through the game's own logic, per player (the nest becomes the village, the village
  becomes the city), so each player's own progression carries over (owner, 2026-11-11).
* No fixed player counts in code paths; data structures hold more than the menus allow.
* Sync what is near (interest management); per-planet sessions later.

## MMO questions to discuss (owner, 2026-11-12: "soon", not blocking the co-op work)

* Players outside your party: their species is another species from your game's point of view, so you can impress
  it or hunt its nest family to extinction by the game's own rules (each player's game keeps its own species
  relationship table, which fits per-player relations). To decide: what happens to a player whose species goes
  extinct (respawn, the repopulation timer); whether players can attack each other's creatures directly (PvP); how
  DNA farming of other players is prevented.
* Co-op code keeps this open: the protection of decision 7 is "same party", never "every seat"; the game's own
  impress / hunt logic stays for every species outside the party.

## Confirmed decisions: 3-4 player co-op with own nests (owner, 2026-11-12)

| # | Decision |
|---|----------|
| 1 | Lineages 54 (the 2026-11-11 fixes) is published first, as its own release, before this work starts. |
| 2 | Each player has their own nest in Creature, about 40-80 m from the party leader's (the host's). A party that came from Cell together lands together. |
| 3 | When the host's species migrates, every nest moves with it, each to its own spot near the host's new nest (the party moves together). |
| 4 | A player who joins with a species from the previous stage keeps it automatically and evolves along with the host. A late joiner without one is asked to create a creature, which is their species for the rest of the game. |
| 5 | A late joiner's home is caught up to the host's home only. Tribe: the same members, hut upgrades and tools as the host's village. Civ: one capital with the same buildings and level as the host's capital (money is already shared). Not the host's whole empire. |
| 6 | A late joiner in Creature gets their own nest of their own species next to the party (the game's own herd-and-nest creation, cGameNounManager::CreateHerd; to verify first; fallback: share a nest with their own creature, as today). At the Tribe start their village comes from their own nest. |
| 7 | Party members' species are best friends with each other: nobody in the party can hunt another member's species or nest, also while that member is offline. Species outside the party can be hunted. |
| 8 | An offline player's nest is run by the AI and can die out naturally; when the player returns, an extinct nest is rebuilt. |
| 9 | Saves that already have a partner village (founded before this work) keep it as it is; only new stage starts use the nest-based creation. |
| 10 | The player cap stays at 4 in the menus; difficulty stays per seat for now (it must become per party for the MMO goal: per-seat scaling does not work at 10-20 players). Data structures hold 8 seats (founders::kMaxSeats). |
| 11 | Tests: up to 3 test games on the owner's PC (5 FPS, small windows unless a test needs a size, e.g. Adventures; console only, never the owner's mouse) plus the MSI over Tailscale; real clicks only on the MSI. |
| 12 | The live Civ test may click in the test host's game window on the owner's PC, once, while the owner is away (owner, 2026-11-12: "1A"). |
| 13 | The host can raise the reserved seats in the middle of a stage (the Co-op window's Set Seats, `coop.seats N now`); the difficulty of the extra seats applies at once. Fewer seats still apply from the next stage; Space keeps "from the next Space load" (owner, 2026-11-12: "2A"). |

## Health check (2026-11-12)

Measured, not guessed:

* Runtime cost is small: the co-op tick averages 0.011 ms per frame (max 2.7 ms), the effects hooks 0.0014 % of the
  main thread (owner's 72-minute session, 3 players).
* Process is solid: 741 tickets, 14,700 lines of reverse-engineering notes, 17 host unit-test suites (all passing),
  483 lab tests, every fix behind a switch so a test can prove it.
* Structure is the weak point, and it matters for the goal:
  * Four files of 5,900-7,500 lines (tribe, coop, civ, cellsync) mix syncing, game rules, UI and test commands.
  * File-wide state (210 globals in tribe.cpp, 161 in civ.cpp, 162 in cellsync.cpp) assumes "the host and one
    partner": 80 `guest()` uses and 16 exact-two-player checks.
  * Every stage keeps its own idea of who owns what (founders in tribe and civ, guest_pack, the nest follow); there is
    no shared ownership layer.
  * 506 switches: each keeps the old code path forever; nothing retires them.
  * The Windows build does not track header changes (a header edit needs FORCE=1; a partial build crashed both test
    guests on 2026-11-11).
  * The Windows and two-PC test scripts of 2026-11-11 live outside the repository (not repeatable by others).

## Plan

Phase 0 (foundation) comes before phases 2-5 (owner, 2026-11-12: "1A").

| Phase | Work | Status |
|---|---|---|
| 0a | Build tracks header dependencies (-MMD depfiles): no more stale objects. | done 2026-11-12 |
| 0b | One ownership layer (`party`): identity -> seat -> owned things (species, nest, pack, village, city) for every stage; the 4-player work goes through it instead of patching the `guest()` spots in place. | done 2026-11-12 (party.cpp, .duoparty) |
| 0c | Split tribe.cpp and civ.cpp along the seams that work touches (founding, ownership and orders, campaign rules); no big-bang rewrite. | decided |
| 0d | The two-PC test harness (MSI tunnel, launch, checks) into tools/ as repeatable tests. | done 2026-11-12 (tools/qa_win) |
| 0e | Switch retirement: a fix stable for 3+ releases loses its switch and old path, in small tested batches. | batch 1 done 2026-11-12 (5 of 506: tribe.guest_attacks, effects.other_stages, core.away_goal, team.rescue_reset, civ.citizen_far; only switches no test or tool flips) |
| 1 | Player identity and the 8-seat founder save formats (DUO2 / CIV2 / OFT2). | done 2026-11-11 (BUG-9135) |
| 2 | Creature: own nest per player (decisions 2, 3, 6, 7, 8), via the game's herd-and-nest creation; respawn and hatch at the own nest; migration together. | done 2026-11-12, two-PC tested (docs/re/creature.md#creature-own-nests) |
| 3 | Tribe: each player's village from their own nest through the game's Tribe-start logic; 3-4 founders in every rule that counts founders (objectives, rivals, alliances, hut upgrades); orders, food, rescue and outfits per seat. | built 2026-11-12 (`tribe.multi_founders`); live-tested with 3 players on two PCs on a copy of the owner's Jiff save: the old broken partner village repaired and bound, a third village founded, orders and food per seat, a rejoin keeps its seat. Not yet live: a fresh Creature -> Tribe start (needs the host's clicks), rescue / give-up, the outfit vote. (docs/re/tribe.md#tribe-multi-founders) |
| 4 | Joining late (decisions 4, 5): detect a species, else the create-a-creature prompt; catch up to the host's home. | built 2026-11-12. Tribe live-tested with four founder villages on two PCs: a joiner on the host's creature is told why and gets the "choose your own creature" row in the Co-op window; their pick founds their village with no rejoin, caught up to the host's (hut level, members, racks); all games agree. Civ live-tested 2026-11-12 with four games: three late capitals (one per partner) caught up with the host capital; a joiner without a creature now gets the same prompt in Civ (fixed after the test showed only a chat notice). Not yet live: the "Create New Creature" editor button (off), BUG-9138's two-waiting-partners case. (docs/re/tribe.md#tribe-late-joiners, docs/re/civ.md#civ-late-capital) |
| 5 | Civ: each player's city from their own tribe; 3-4 nations in the campaign rules (with BUG-9054, BUG-9056, BUG-9073). | built 2026-11-12 (`civ.multi_founders`), host unit test. The founder roster, seat owners and shared money live-tested with four nations through late capitals (phase 4). The graduation itself (Tribe -> Civ with 3-4 founder villages) is not yet live: it needs a Tribe save with five objectives done; none exists on this PC and the game has no stage-advance cheat (docs/06-testing.md#testing-two-pc-msi). (docs/re/civ.md#civ-multi-founders) |
| 6 | Space: the personal launch per seat. | planned |
| 1b | Stable species keys per identity (a partner's creature keeps its key across rejoins under another network id). | planned |
| 7 | Adventure creatures simulated by the host (BUG-9114). | waiting (needs clicks on the owner's PC or the MSI) |

The phase details (code spots, formats, messages) are in docs/tickets/BUG-9135-no-own-villages-three-players.md.

## Decided (2026-11-12)

* Phases 2-5 are built on the phase 0 foundation, not by patching the two-player code in place (owner: "1A").
* Switch retirement: a fix stable for 3 or more releases loses its switch and its old code path, in small batches,
  each batch with the host unit tests and a two-PC smoke test (owner: "2A").

## Open decisions

* (2026-11-12, both answered: decisions 12 and 13.) Seats added mid-stage: built and live-tested 2026-11-12 on the
  Jiff test copy: 3 reserved, a fourth player refused ("session is full"); `coop.seats 4 now` on the host; the host,
  both guests and then the fourth player read seats=4 (enemy health 130% -> 145%) at once; the fourth player joined
  (4/4) and commanded their old village again (seat 1, by identity).
* The late joiner's creature (owner's question, 2026-11-12): "Why are we having them pick a village and creature on
  late join? Shouldn't they be creating a creature while the village is setup?" Answer: they pick no village (it is
  placed for them); a village (and a capital) is born with its species, so it waits for the creature; the "choose from
  your creations" row was built because the editor path (decision 4's "create") is untested and off. Asked: A open
  the creature editor at once and prepare the site and catch-up meanwhile (the home appears about 15 s after they
  save; existing creations stay as a shortcut), B found the home at once with the host's creature and swap every
  villager afterwards, C keep the choice row with the editor button beside it.
* The Tribe -> Civ graduation test with four founder villages (phase 5): needs a Tribe save with five objectives.
  Asked 2026-11-12: run the lab's scripted raids on the test host window (about an hour of clicks), test it in the
  owner's next co-op game, or wait.

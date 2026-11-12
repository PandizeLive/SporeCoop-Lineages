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
* Space (briefing 2026-11-12, research only): today every player flies in their own copy of the host's galaxy, linked
  by star and planet ids; one empire for the whole session (shared money and diplomacy, spacecoop / shared.cpp), an own
  ship and captain per founder, enemies shared only in the host's star system (squadron, hard-wired to player 0), a
  guest's new colonies shown to the others as an allied "<name>'s empire" and given back on a rejoin from the host's
  memory only (space_allies, space_map), the Galactic Core waiting for the whole session, difficulty per slot. For 3-4
  players: the Civ -> Space hand-off carries two founders only (world.cpp, civ.cpp), and Space does not use the party /
  identity layer yet (keyed by network id or name). Carries over to the MMO: one game per area, id-linked copies,
  allied empires of other players, Rivals mode's own budgets. Would be redone: host = player 0 in charge, waiting for the
  whole session, keying by name, colonies in memory only, per-slot difficulty. Decisions asked 2026-11-12: what a
  player owns in Space (recommended: an own empire each, a party as a standing alliance), phase 6 scope (recommended:
  keep one shared empire for co-op now but move Space onto the party / identity layer, launch every seat, colonies saved
  per identity in a side file), who runs enemies in a system (recommended: whoever of the party is there, handed over
  when they leave), where Space progress is saved (recommended: a side file per identity), difficulty (recommended: per
  party), the Galactic Core (recommended: your own party only).
* Space, rethought (owner 2026-11-12: "Consider the idea that multiple people will be progressing out of the planet,
  and especially in MMO, non partied members may progress out the planet too."). The native end of Civ is "own the
  whole planet", which with several parties on one planet means beating the other players first. Asked 2026-11-12:
  what ends Civ for a party (recommended: the party's own progress target, the other parties keep playing Civ);
  what happens to a party's cities when it leaves (recommended: they stay as its homeworld, nobody can capture them);
  who launches (recommended: the whole party together, each player in their own ship; a later joiner launches from
  the homeworld); what Space players may do to a planet where others still play Civ (recommended: visit and trade
  only, no raids or abductions of players' cities).
* These Space questions are decisions 1X-2C and 2H-2K in the decision log (Open decisions).

## Confirmed decisions: 3-4 player co-op with own nests (owner, 2026-11-12)

These are 1A-1K, 1N-1Q in the decision log (Open decisions); the numbers below are kept.

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
| 14 | A late joiner without a creature of their own (owner, 2026-11-12: "1B ... make it clean and seamless; if not possible, 1A"): Tribe: their village is founded at once with a stand-in that looks like the host's creature (a re-keyed copy, so every per-seat rule holds), caught up, and switches to their own creature in place (the outfit path, no villager replaced) when they make one. Civ (no clean species change known): 1A: the creature editor first; meanwhile a blurred view (the host's, an AI's or their home's camera) with a progress ring and short steps ("Catching up to Host...", "Matching tools...", "Buildings are rising...", "Final touches..."). Research: docs/re/tribe.md#tribe-late-joiners (stand-in notes to add with the build). |
| 15 | The owner calls the reserved seats "Player Slots" in every text; the lobby has an Apply button; no Send World button (the world goes out by itself). |
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
| 4 | Joining late (decisions 4, 5): detect a species, else the create-a-creature prompt; catch up to the host's home. | built 2026-11-12. Tribe live-tested with four founder villages on two PCs: a joiner on the host's creature is told why and gets the "choose your own creature" row in the Co-op window; their pick founds their village with no rejoin, caught up to the host's (hut level, members, racks); all games agree. Civ live-tested 2026-11-12 with four games: three late capitals (one per partner) caught up with the host capital; a joiner without a creature now gets the same prompt in Civ (fixed after the test showed only a chat notice). Tribe stand-in villages (decision 14, BUG-9150) built and live-tested 2026-11-12: a joiner without a creature gets a village at once that wears the host's creature, and their own creature replaces it in place. Not yet live: the "Create New Creature" editor button (off), BUG-9138's two-waiting-partners case, the Civ loading screen (decision 14, Civ part). (docs/re/tribe.md#tribe-late-joiners, docs/re/civ.md#civ-late-capital) |
| 5 | Civ: each player's city from their own tribe; 3-4 nations in the campaign rules (with BUG-9054, BUG-9056, BUG-9073). | built and live-tested 2026-11-12: a native Tribe -> Civ graduation with four founder villages (TITAN host + 2 TITAN guests + the MSI) founded one capital per founder seat, each with its own species and commanded by its own player, on every game; 0 faults. (docs/re/civ.md#civ-four-founders-live) |
| 6 | Space: the personal launch per seat. | planned |
| 1b | Stable species keys per identity (a partner's creature keeps its key across rejoins under another network id). | planned |
| 7 | Adventure creatures simulated by the host (BUG-9114). | waiting (needs clicks on the owner's PC or the MSI) |

The phase details (code spots, formats, messages) are in docs/tickets/BUG-9135-no-own-villages-three-players.md.

## Decided (2026-11-12)

* Phases 2-5 are built on the phase 0 foundation, not by patching the two-player code in place (owner: "1A").
* Switch retirement: a fix stable for 3 or more releases loses its switch and its old code path, in small batches,
  each batch with the host unit tests and a two-PC smoke test (owner: "2A").

## Open decisions

Decision log (owner, 2026-11-12): every decision ever asked has a permanent ID in the order it was asked: 1A, 1B ...
1Z, then 2A ... 2Z, and so on. Answers read "2HA" (decision 2H, option A). Options: A what Claude recommends, B the
closest to the owner's ask (with its issues), C an alternative; one answer per ID, no sub-decisions.

| ID | Decision | Answer |
|---|---|---|
| 1A | Lineages 54 is published first, before the 3-4 player work (table above: 1) | A |
| 1B | Own nest per player in Creature, 40-80 m from the host's (2) | A |
| 1C | Nests migrate together (3) | A |
| 1D | A joiner keeps a species from the previous stage; a late joiner without one makes a creature (4) | B, with the owner's detection rule |
| 1E | A late joiner's home is caught up to the host's home only (5) | A |
| 1F | A late joiner in Creature gets their own nest (6) | A |
| 1G | Party species are best friends; other species can be hunted (7) | A |
| 1H | An offline nest is run by the AI, can die out, is rebuilt on return (8) | A |
| 1I | Old saves keep their partner villages (9) | A |
| 1J | Cap 4 in the menus, difficulty per seat for now, data for 8 seats (10) | A |
| 1K | Tests: up to 3 test games on the owner's PC at 5 FPS plus the MSI; real clicks on the MSI (11) | A |
| 1L | Phases 2-5 built on the phase 0 foundation | A |
| 1M | Switch retirement in small tested batches | A |
| 1N | The live Civ test may click in the test host's window (12) | A |
| 1O | The host can add player slots in the middle of a stage (13) | A |
| 1P | A late joiner without a creature: a stand-in village at once, else the editor first with a loading screen (14) | B in Tribe (built, BUG-9150); A in Civ (the loading screen, not built yet) |
| 1Q | "Player Slots", an Apply button, no Send World button (15) | owner's request, built |
| 1R | The Tribe -> Civ graduation test by scripted native raids | A (done, phase 5 live) |
| 1S | A save loaded with fewer slots than players connected gets more slots | A (built, BUG-9143) |
| 1T | The "Give To" button | B, removed (BUG-9148) |
| 1U | Party wipe: when everyone is down, the newest won village becomes a rival again, the top totem piece and one progress step are lost with a red marker, unlocks and DNA kept, a 10% food debt when nothing was won; Civ: the newest captured city goes back | A |
| 1V | Downed: the creature plays its native dying pose while downed | A |
| 1W | Downed: spectate the nearest partner, "SPECTATING <name>", Tab and left / right arrows next to the name | A, with the arrows |
| 1X | Space: what a player owns | A (owner: "A party will always progress together and share.") |
| 1Y | Space: what phase 6 builds now | A; owner asked how an empire works for a planet that solo players leave (see 2N) |
| 1Z | Space: who runs the enemies in a star system | A |
| 2A | Space: where Space progress is saved | A |
| 2B | Space: how difficulty scales | A; owner asked whether parties need a friend list and whether to switch from slots now (see 2O) |
| 2C | Space: the Galactic Core ending | A |
| 2D | Creature: the "cocoon" | the native cocoon (owner: NPC species wrap into a cocoon and come out as a new species; Claude was wrong that the game has none) |
| 2E | Creature: how evolving starts | owner's rule: native mating call; the nearest mate answers, else one is spawned; the player clicks the mate to start |
| 2F | Creature: DNA when two players evolve at once | B, personal DNA (owner: "Everyone gets their own DNA.") |
| 2G | Creature: who moves the party's nest | owner's rule: anyone starting the native migration warns every player in the world, 10 s later all go into the editor for final touches (downed or dead players too, cleaned up) and migrate together |
| 2H | Space: what ends Civ for a party | A |
| 2I | Space: a party's cities when it leaves the planet | B (owner asked whether a leaving party needs its cities at all, see 2N) |
| 2J | Space: who launches | A |
| 2K | Space: what Space players may do to a planet where others still play Civ | A (owner asked what happens when 100-1000 players fill the galaxy: MMO research, in the Space notes) |
| 2L | Tribe: when the Village page goes | B, now, prioritised (owner: "prioritize this"); page removed 2026-11-12 (BUG-9153), the native hut and tool menus in progress |
| 2M | Tribe: the creature editor for a joiner without a creature | B, the editor first, the village after they save |
| 2N | Space: the home of a player or party that leaves a planet others still share | open |
| 2O | Parties: several parties in one session now, or the session stays one party for now | open |

Next free ID: 2P.

Answers of 2026-11-12 (1X-2M), notes:
* 2D: the game has a cocoon. NPC creatures in the Creature stage wrap into a cocoon and come out as a new species
  (SporeWiki, Evolution / Cocoons). The personal evolving uses that cocoon instead of the player's egg: the mating call
  (2E), the player clicks the mate, the creature enters the cocoon, the editor, it comes out as the new creature.
  To find: the cocoon model and the NPC species-evolution code that plays it (docs/re/creature.md).
* 2G with 2F: DNA is personal, the progress (brain) stays the party's; whoever starts the native migration starts it for
  everyone (10 s warning, everyone into the editor, migrate together).
* 2M changes 1P for Tribe: a joiner without a creature goes into the editor first; their village is founded when they
  save (with the loading screen of 1P A). The stand-in village (BUG-9150) stays only as the fallback when they leave
  the editor without saving.
* 2K: the galaxy has about 42,000 stars (a community count, TheOnlyZac/starmap); planets are made when a star is first
  visited and almost every system can be colonised (terraforming). A Space empire holds tens of colonies, so about 1000
  players could fill one galaxy. For the MMO: several galaxies (one per server) is the known-safe answer; adding stars or
  planets to one galaxy needs research first (unknown).

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
* (2026-11-12, answered "1A": done, phase 5 live-tested.) The Tribe -> Civ graduation test with four founder villages (phase 5), owner 2026-11-12 "2A, be smart": on a fresh
  copy of the Jiff save four founder villages came back in a minute (seat 1 bound with `coop.tribe bind_founder 1 2`),
  but the shortcuts to five wins failed: five totem pieces (Totem_AddSegment) plus the player's progress at 5 / 5
  (cPlayer +0x10F0) do not make History offer the next stage; an NPC village made with `coop.tribe npc_tribe` and its
  hut destroyed with the host's faction (cTribeHut_TakeDamage) counts as no win. The game offers Civ only after a native
  win through its goal path (a target chosen by TribeGoalUI_SelectTarget, raided by ordered villagers:
  tools/tests/duo_tribe_campaign.py). Asked again 2026-11-12.
* (answered 2026-11-12, "2A" and "3B", built: BUG-9143, BUG-9148.) BUG-9143: a save loaded with fewer player slots
  than players connected now gets more slots; the "Give To" button is removed.
* Party wipe (owner 2026-11-12: "When all players go down or die in a stage like tribal ... lose a totem/progress and
  a captured location with the red debt icon ... They keep their unlocks and DNA ... This can work in Civ as well."):
  researched 2026-11-12, design and its decisions asked (when it applies, which village, how it comes back, the totem,
  none won yet, Civ). Note: the native "bounce back" reloads the slot the mod autosaves every 10 minutes and after
  every win, so today's loss is up to 10 minutes, not the wins.
* Space and the MMO goal (owner 2026-11-12: "We still need to discuss Space Stage"): briefing sent 2026-11-12 with six
  decisions (what a player owns in Space, phase 6 scope, who runs enemies, where Space progress is saved, difficulty
  scaling, the Galactic Core).
* Downed players (owner 2026-11-12): the dying animation while downed, spectating a partner with a clear HUD and a
  native vignette. Proposal asked.
* Creature: a personal editor with the native cocoon (owner 2026-11-12: "Now that we'll have personal nests, we should
  allow each player to enter the creator on their own accord. While a player is in the editor or evolving ... apply
  the evolving cocoon native to the game. They will enter the cocoon with the native animation when starting the
  editor. When they confirm their creature, they can pop out of the cocoon with the native animation with the new
  creature. It should be very natural and immersive for an outside viewer of the nest."). Replaces the evolution vote
  in Creature. Research 2026-11-12: Spore's Creature stage has no cocoon; its own evolving sequence is the mating dance
  (hearts), an egg in the nest (cEgg, its laid / hatched effects), the editor, then the creature hatches as a baby and
  grows up (CreatureGame_AfterEditor, GrowTo). Partners today see the editing player's puppet vanish and come back
  adult-sized. Decisions asked 2026-11-12 (the egg as the "cocoon", how evolving starts, the host's editor, DNA when two
  evolve at once, migration). A likely bug found on the way: BUG-9151.

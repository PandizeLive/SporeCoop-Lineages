---
id: BUG-9175
title: Creature: a guest's mating with an own-family copy stalls after the click (no animal target)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs: docs/re/creature.md#creature-mate-keeper
related: BUG-9174 BUG-9160 BUG-9161 BUG-0177
---

## Report
Found while testing BUG-9174 (2026-11-13, the MSI as a guest of the TITAN host, Samuth): a click on the own-family copy offered as mate starts the mating (hearts over the player's creature and the family) and nothing follows: the creature stands in the mating action for minutes, no partner comes, no cocoon. The owner saw the same on 2026-11-12 ("getting stuck on the center of the nest"); clicking the nestmate again made them mate then.

## Root cause
Observed: `coop.evolve mates` after the click: action 02D852F0 (the mating approach), target 00000000, the native mating step (0xD6D560) called every frame with state 0 -1 -256 0, reached avatar 1 mate -1. Inferred: the mating needs the mate's own AI to answer and walk in (0xD6D360, the answer that also starts the hearts); a copy's AI is the host's (npc.cpp moves it), so the guest's game never gets an answer. Ideas: the game's own spawned mate on a guest (0xD52B86 spawns one of the avatar's species near the nest when none is eligible: decision 2E "else one is spawned"), kept by the guest's NPC filter; or the copy's AI handed over for the mating.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.

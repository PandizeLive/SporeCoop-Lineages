---
id: BUG-9176
title: Creature: a guest's goal card says its own species is extinct (the host-key copies all converted)
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
docs:
related: BUG-9169 BUG-9174
---

## Report
Seen on the MSI as a guest (2026-11-13, screenshot): the goal card "Hunt 6 Lilku" turned into "The Lilku species is extinct!" (twice). Lilku is the player's own creature.

## Root cause
Observed: on the guest the host's key of the player's own creature (7CD76455, later 7C5EE376) is a second species with the same name; its only members are the copies of the player's family, and npc.own_family_species (BUG-9169) gives them all the player's own species. Inferred: the game counts the second "Lilku" as extinct once none is left of it, and its hunt goal (it is not the player's species there) reports it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.

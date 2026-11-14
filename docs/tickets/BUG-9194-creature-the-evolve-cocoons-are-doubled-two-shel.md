---
id: BUG-9194
title: Creature: the evolve cocoons are doubled (two shells on each creature)
status: fixed
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits:
tests: tools/qa_win (to run: a cocoon at full frame rate)
signature:
docs:
related: BUG-9181
switch: creature.cocoon_one_spot
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "I also noticed that the cocoons are doubled" (a screenshot: a second shell overlapping one cocoon).

## Root cause
Inferred (code): keep() lays a new woven shell every kShellEvery 1.8 s, each living ~2.0 s, at the creature's position then; the overlap of a held puppet's drifting position showed two shells side by side, at the owner's frame rate (the earlier checks ran at 10 FPS).

## Fix
creature.cocoon_one_spot (on): every shell and sac of one cocoon at the spot it went in (Cocoon::spot).

## Verification
no-fail-run: not run again live yet (the owner tests the build on their install).

## History
- 2026-11-13: opened.

---
id: BUG-9183
title: Tribe: a guest whose own village is wiped out sees the game's own game-over screen
status: open
kind: bug
stage: tribe
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
related: BUG-9147 BUG-9155
---

## Report
Found in the 2Z A check (2026-11-13): the MSI as a guest on a worn copy of the Jiff Tribe save (DuoQAJiffStand, every village down to one villager): the mod said "MSIGuest's village was wiped out: no win to lose yet: every village lost 10% of its food" (the party wipe, BUG-9155) and the MSI's game showed the native game over too ("Alas! Your population has fallen below the minimum needed to survive ... Bounce back in time", out/shots/guest-native-gameover.png). BUG-9147 keeps the host's game from it; a guest's own game was not checked.

## Root cause
Not analysed yet. Inferred: the guest's game runs the population event (05C80207) for its own player tribe as the host's does; BUG-9147's hold is on the host only.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.

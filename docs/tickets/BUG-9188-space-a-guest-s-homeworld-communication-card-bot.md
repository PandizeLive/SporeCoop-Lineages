---
id: BUG-9188
title: Space: a guest's homeworld communication card (bottom left) stays blank after a personal launch
status: open
kind: bug
stage: space
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
related: BUG-9187
---

## Report
Seen in the four-founder launch (2026-11-13): on each guest (Late, Fourth, the MSI) the homeworld communication card next to the minimap ("Gentmin E...") is a blank white picture, still blank after the welcome captions (out/shots/guest-space-blank-card.png); the host's shows its species (out/shots/host-space-card.png). Cosmetic.

## Root cause
Not analysed. Inferred: the card shows the homeworld's species (the host's creature, 291E99C5 here) and a guest's game has no thumbnail for it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.

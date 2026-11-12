---
id: BUG-9147
title: tribe: the host's last villager killed by the co-op epic without the knockout, native game over
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: found in QA 2026-11-12 (four-player Jiff test copy)
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9135 BUG-0172
---

## Report
QA 2026-11-12, four players on the Jiff test copy (every founder village down to one member after 4 idle hours): `coop.tribe epic` near the host's village; the host's chief died (hp 0, dead=1) without "Last villager down" and the host got the native game-over caption ("Alas! Your population has fallen below the minimum...").

## Root cause
Not analysed. protect_founder (tribe.cpp) refuses a knockout when the village is not intact (hut gone or damaged to 0), when no partner village has a living member, or when the hit is not lethal on that call; the epic's attack path is not known to go through the citizen damage hook. Earlier the same day two epics did knock villagers out.

## Fix
Not fixed.

## Verification
no-fail-run: not analysed yet.

## History
- 2026-11-12: opened.

---
id: BUG-9089
title: Adventures only share a single move-to objective and partner markers
status: open
kind: bug
stage: Adventure
reported: 2026-11-07
source: User-approved full Adventure implementation
role: host guest
versions: 0.7.3.6-lineages25-dev
fixed_in:
branch: feat/lineages-adventures
commits:
tests: tools/tests/duo_adventure.py
signature: single_gate excludes talk, combat, parallel goals and terminal rewards
docs: docs/re/space.md#space-duo-adventure
related: BUG-9003 BUG-9004
---

## Scope and accepted decisions

The user has reopened full shared Adventures after the playable-through-Space
pass. Each founder keeps their own captain. The speaking player chooses when
dialogue changes the mission; the host commits that shared choice. Support up
to four participants in the Adventure design, independent of the unfinished
four-founder campaign bindings. Use targeted scenarios, not full-stage replays.

## Confirmed baseline limitations

adventure.cpp only credits a single move-to goal required once. coop.cpp only
ticks it with exactly two players. The Adventure adapter deliberately has no
native partner spawn function after BUG-9003; partners are markers. Native
NPC replication excludes Scenario mode. Dialogue, combat, parallel objectives,
mission results and captain XP are currently independent simulations.

## Implementation order and completion checks

1. Reproduce the first talk-goal divergence and inspect native goal/actor identity.
2. Establish host-authoritative run/act identity and idempotent shared progress.
   Reject stale runs, duplicate credits and mismatched mission content.
3. Bind safe native captain representations and scenario actors. Synchronize
   combat and object interactions without duplicating collectible/kill credit.
4. Synchronize speaking-player dialogue and all objective types, including
   parallel goals, defend/failure conditions and timed acts.
5. Qualify native success/failure, each captain's eligible reward exactly once,
   disconnect/rejoin, and return to the same Space campaign and personal ship.
6. Package only after native peer checks, clean exits and focused regressions.

## Current verification

Baseline FAIL: build/local/adventure-talk-baseline-fail.log records host act 2
and guest act 1 after the native mayor conversation. Required-once Talk goals
now use a host journal. PASS: adventure-talk-patch-a.log (host speaker),
adventure-talk-patch-b.log (guest speaker), and adventure-talk-reconnect.log
(guest offline during final parallel goal, catches up into act 3). All native
summaries/goals agreed, idle players stayed still, and fault counters were zero.
The first gate also passed on lineages25 before changing code.

The journal routes to all four session IDs, but these live runs used two games.
Packet truncation/bounds tests and the new DLL build pass. Native Replay followed
by the first gate passed on the new journal (adventure-new-journal-gate.log).
Opt-in captain bodies survived two same-act native resets per peer, but have
incorrect visual scale and remain disabled by default. Their native captain
export is fixed. Combat, actor replication, all other objective types and
terminal rewards remain open. Both games shut down natively with zero faults
and exit code 0. Marker-mode player-object sync mismatches remain. This ticket
is not fixed and full Adventures are not complete.

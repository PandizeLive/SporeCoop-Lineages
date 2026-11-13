---
id: BUG-9167
title: Creature: a player's new nest is sent before it has a member; the guest's empty copy dies out and the player never moves in
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: qa
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + 2 TITAN guests + the MSI as a guest over Tailscale (relayed), 2026-11-13, all players with a creature of their own)
signature:
docs: docs/re/creature.md#creature-migration-own-nests
related: BUG-9135 BUG-9128 BUG-9166 BUG-9168
switch: creature.nest_share_member
---

## Report
QA: after a migration the players' new nests were sent at once; on the MSI its new nest had no members ("our home nest (218 -442 -113) has no members here yet: joined later", every 10 s) and a few minutes later was gone from its game; the player stayed in the host's nest.

## Root cause
Observed: a nest herd made by CreateHerd has no members; cHerd_Update makes the first one when its respawn time (+0x11C, 120000 ms on these herds, mem.read) has run. The save for the share was made within a second, with an empty herd. On the guest the empty copy cannot be joined (BUG-9128 waits for members: an empty herd faulted the claim) and can die out.

## Fix
creature.nest_share_member (on), home_nests.cpp: a new player nest gets 2 s respawn time until its first member (then the game's value again); its world goes out once it has a member (share_due_tick, 90 s at most). coop.homes shows "its world goes out with its first member" while it waits.

## Verification
no-fail-run: manual. Before: 91 s and still 0 members (the 120 s respawn time), shared empty. After: "seat 1's world goes out 4 s after its nest was made (1 member(s))", seat 2 after 4-6 s; both guests joined their new nests (with BUG-9168).

## History
- 2026-11-13: opened.

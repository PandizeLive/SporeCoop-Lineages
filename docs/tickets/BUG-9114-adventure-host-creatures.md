---
id: BUG-9114
title: Adventure creatures move differently in each game; the host should simulate them for everyone
status: open
kind: feature
stage: Adventure
reported: 2026-11-10
source: owner request (44-dev)
role: host guest
versions: 0.7.3.6-lineages44-dev
fixed_in:
branch: feat/adventure-bodies
commits:
tests: tests/adventure_sync_tests.cpp (hits, creature batches); QA compare of `coop.adventure actors` on both games
signature: enemies' positions differ between games; host creatures pick a partner body but never strike it
docs: docs/re/space.md#space-adventure-host-creatures
related: BUG-9089 BUG-9113 BUG-0016
switch: adventure.enemies
---

## Report

Combat adventures (BUG-9089) share deaths, credits and the end, but every game moves its own enemies,
allies and characters. An enemy chasing one player stands still on the other's screen until it dies.

## Implementation (44, behind `adventure.enemies`, off by default)

* Host: every 0.1 s the states of the scenario's creatures (key = host object serial, id = scenario
  object id, position, facing, velocity, health, dead) that changed, all of them once a second,
  unreliable (MSG_ADVENTURE_ACTORS, 20 per packet).
* Guest: its scenario creatures have no AI tick (`game::set_ai_skip`); each host creature is paired
  with the nearest local creature of the same id and driven like a partner puppet (walk on the path,
  else the driver), with the host's health. A death of a paired creature comes by key (killing "any
  one of that id" killed living lookalikes: soldiers from one marker share an id); its corpse is put
  where the host's lies. The guest captain's hits on them go to the host (Packet Hit) and are applied
  there with the guest's body as the attacker; other damage to them is dropped.
* Host: npc `ai_target` also runs in an Adventure, so a creature nearer to a partner's body ticks
  with that body as the player.

## Verification so far

City Assault, QA instances 9 + 8, sync on from the mission's start, 40 s of the allied army's battle:
living creatures paired by key 75, p50 0.38 m, p90 1.32 m, max 2.80 m, health equal on 74 of 75
(`compare.py`); before deaths by key a run had 30 extra living creatures on the guest. 0 faults.

## Open (why it is off)

The host's creatures picked the guest's body (npc `partner_picks` 23-34 in a minute) but never struck
it: no "1 hit by" damage line on the host, guest `damage_in` 0. With the switch on, the guest's own
creatures have no AI, so the guest was hurt only by buildings (a turret's 30 every 3 s): combat would be
trivial for guests. Next: how scenario creatures choose an attack target (the scenario's combatant /
political target system rather than the behaviour tree's avatar, docs/re/creature.md 14); the body's
political id; whether the body needs the captain's combatant registration. The guest-hit path (Hit ->
host) was not exercised before the mission ended. Not tested: two PCs, 3-4 players.

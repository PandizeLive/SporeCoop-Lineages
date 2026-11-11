---
id: BUG-9129
title: Creature: the guest has no pack: Summon Flock does nothing and the host's creatures cannot join the guest's party
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: owner reports on 0.7.8-lineages49-dev ("Summon Flock isn't working for the connected player still"; "connected player is unable to create a party with the host's creatures")
role: guest
versions: all
fixed_in:
branch:
commits:
tests: QA Samuth.spo host 9 + guest 8: `coop.drama start 8` on the guest (the event the ability's node starts), screenshots of both
signature: guest log "drama: summon event starts (guest)", the guest's status "Pack: alone", nothing on the host
docs: docs/re/creature.md#creature-roar-relay
related: BUG-0556 BUG-0501 BUG-0418
switch: -
---

## Findings
* Summon Flock is the player's consequence ability 0x33: BT_SpecialAbilityTick 0xD8E840 -> DramaManager_StartKind(8)
  -> cSummonCreatureEvent (start 0xD50EA0), which makes a herd and adds each creature to the avatar's pack
  (cPosseSimulator_AddMember 0xD542E0). On a guest the event starts in its own game (log above) and no flock is
  left: the host owns every NPC (npc.cpp removes the guest's own), and the host never hears of it.
* Recruiting (befriending a creature into the party) is the same missing piece: the pack is the avatar's posse in
  each game; the host's game has one posse (the host's), the guest's game has no NPCs of its own to keep.
* A fix needs the host to run a second pack that follows the guest's puppet: the summon / recruit as a request to
  the host (as the roar relay, BUG-0501), the members added there and steered after the puppet, sent to the guest
  as NF_PACK proxies (BUG-0556 makes them allies). Not started.

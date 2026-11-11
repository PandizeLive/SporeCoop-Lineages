---
id: BUG-9132
title: Creature: the guest's Strike does no damage to the host's creatures (Bite does)
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages50-dev
role: guest
versions: 0.7.8-lineages50-dev
fixed_in:
branch:
commits:
tests: QA Samuth.spo host 9 + guest 8: guest `coop.abilities target <npc>` + `coop.abilities play <i>` beside the proxy; host log "npc: guest 1 hit npc ..."; guest log "npc: proxy N hit here"
signature: -
docs: -
related: BUG-0554
switch: -
---

## Findings
* The guest's abilities (`coop.social abil -1`): 0x1E bite (points 1, range 1), 0x1F charge, 0x20 spit, **0x21 strike
  (points 6, range 1, effect kind 3 = area, chance 1.0)**; 0x1D is social (kind 1, range 100).
* Bite: the guest's TakeDamage on the proxy goes through npc::damage_filter (attacker = own avatar) to the host:
  "guest 1 hit npc 39: reported 1.00 type 2 applied 1.00, hp 34.50 -> 33.50".
* Strike (ability 10): no call of the damage filter on any proxy was logged on the guest and no hit reached the host.
  Its damage seems to come from the area effect of CreatureAbilityHit 0xC20390 (effect kind 3), not a TakeDamage with
  the avatar as attacker. Next: log CreatureAbilityHit / the area effect on the guest for the strike (attacker,
  targets, amount) and forward it like a bite. The QA runs were noisy (NPCs knocking the guest out).
* Diagnostics added: host "npc: guest %u hit npc %u: reported .. type .. applied .., hp .. -> .." and guest
  "npc: proxy %u hit here: attacker .." (rate limited), `coop.abilities target <npc>`.

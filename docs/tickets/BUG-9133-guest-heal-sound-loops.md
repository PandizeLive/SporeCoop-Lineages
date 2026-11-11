---
id: BUG-9133
title: Creature: the connected player hears a heal sound repeating after a fight, healing at a nest
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages50-dev ("the animation for the heals is gone, but the audio is still there for the connected player"; "after a fight and start healing at the extinct nest")
role: guest
versions: 0.7.8-lineages50-dev
fixed_in:
branch:
commits:
tests: -
signature: -
docs: -
related: BUG-9126
switch: -
---

## Findings (not reproduced)
* BUG-9126 (regen topping up hunger's drip every second) removed the looping heal visual.
* QA Samuth.spo host 9 + guest 8 at the host's nest: `coop.audio mark` / `list`: the most frequent play on both games
  is sound 0xCF70F2F0 from 0xC6749B <- 0xCB3B18 <- 0xCA2A87 <- 0x919EA1 (an effect's sound, new track every play):
  host 41 in 10 s, guest 107 in 20 s; the same on both, so likely ambient (the nest), not the report.
* A host family creature hurt at the nest (hp 15.5 -> 7.5) is not healed by the host's game and its proxy on the guest
  stays at the host's value: no heal fight there.
* The guest's own health after a respawn goes 15.5/15.5 -> 31/31 (the brain level's HP) and stays: the regen does not
  chase an unreachable max.
* Next: a real session's report (`/bug heal sound` in the co-op chat when it loops) with the sound list
  (`coop.audio list`) of that moment.

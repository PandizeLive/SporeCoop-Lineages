---
id: BUG-9181
title: Creature: evolving the native way (mating, egg, editor), the family in cocoons, the player hatching from the egg
status: open
kind: feature
stage: creature
reported: 2026-11-13
source: owner
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs: docs/ROADMAP.md (2E, the owner sequence)
related: BUG-9160 BUG-9161 BUG-9174 BUG-9175 BUG-9180
switch: creature.cocoon creature.egg_watch creature.mating_direct creature.guest_mate_offer
---

## Report
The owner tested (2026-11-13, the MSI as a guest, TITAN watching): a click on the mate went straight into a cocoon instead of the mating; only the player was in a cocoon, its body poking through; back from the editor it started in its egg, then turned into a cocoon; TITAN saw the guest's mating call waves the whole time. The owner's sequence, closest to the game: 1. the player clicks the mate and walks to it; 2. the game's scene, the dance; 3. the player lays an egg in the middle of the nest; 4. the editor. Then (the mod): 5. every living family member (not the egg) spins into the game's cocoon; 6. they stay in it while the player edits; 7. back: the family comes out first (the game's spin, animation and sounds); 8. the player hatches from its egg with the game's own scene, native from there.

## Root cause
Observed: on a guest the mate is a copy of the host's member (npc.cpp: a puppet, NPCTickAI skipped), so the game's pairing never got its answer (BUG-9175) and the mod went to the cocoon after 2 s of hearts; the own egg scene was replaced by a cocoon (BUG-9161); the own creature played an emerge after the editor (own_tick) on top of the game's hatch.

## Fix
npc.cpp lend_local_ai: a proxy runs its own AI on the guest for a while (puppet unregistered, the sync leaves it alone, back to the sync after). coop.cpp mate_keeper: the mate offered and the one the player mates with are lent, 2 s at a time while they are. evolve.cpp: the egg scene is the game's (no cocoon of the own body; Escape after 12 s, BUG-9161); when the editor opens the own family (its species within 50 m, never the egg) goes into the cocoon, on partners' games with the partner's cocoon (puppet_editing); back, the family comes out first (family_out); the player has no emerge of its own (it hatches from its egg); a partner's puppet leaves its cocoon without an emerge. The direct editor only after a 30 s stalled pairing (was 2 s on a guest).

## Verification
Not yet: built and on all three games (2026-11-13); the click on the mate on the MSI needs the owner (remote clicks at 5 FPS miss a walking mate). Seen: the keeper lends the offered copy (keeper calls 5808, lends 2210), the copy then walks on its own (31 m from the nest).

## History
- 2026-11-13: opened.

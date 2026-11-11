---
id: BUG-0467
title: a Cell part the guest picks up is not unlocked for the host
status: fixed
kind: bug
stage: Cell
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in:
branch: feat/lineages-45
commits:
tests: QA host 9 + guest 8 (Cell, Cellfive.spo), coop.parts cellpick, see Verification
signature:
docs: docs/re/cell.md#cell-parts-shared
related: BUG-0401 BUG-0316 BUG-0446
switch: parts.cell_shared
---

## Report
Host's mark (s 433, 17:31): 'My guest just took a new piece in cell stage but i dont get it unlocked.' parts.shared (BUG-0401) covers Creature only; Cell part tokens are per game (BUG-0316). Class G4.
Target: 0.7.4. Owner request (2026-11-10): parts either player unlocked, also before the session, are everyone's.

## Root cause
The Cell stage keeps its parts in 13 slots of the Cell player's data (S+0x34 + 4*part, S = cCellGame
+0x5190; 0 locked, 1 unlocked in a save, 2 just collected), not in the Creature's collected items that
parts.shared (BUG-0401) shares. Nothing sent them. Observed (QA, sharing off): the host's slot 12 and the
guest's slot 1 each stayed in their own game.

## Fix
The Cell slots join parts.shared's host-held union as keys {0xCE11BA57, slot}: a guest sends the slots
it has and the host lacks, the host unlocks them (2, and the mating-call button S+0x68 / S+0x69, as a
collected token does: 0xE5D7B0) and sends its set, every guest in the host's world unlocks what it lacks.
On a join both games' slots merge (retroactive: what either player had is everyone's). The host's set is
per world and stage (Cell slots and Creature items are separate sets). Switch: `coop.fix parts.cell_shared off`.

## Verification
Windows QA (Steam copy, GOG 2024 code), host 9 + guest 8 in the host's Cell world (Cellfive.spo), 10 fps.
`coop.parts cellpick <slot>` writes a slot as 0xE5D7B0 does.
* OFF: host picked 12, guest picked 1: host 0011002101212, guest 0211002101210, each its own (the bug).
* ON: within 8 s both 0211002101212 (earlier pickups merged); a live guest pickup (slot 10) reached the
  host within 6 s. 0 faults on both. The guest's mating-call button showed after the share.
* Pitfall: a pickup right after a join can be replaced by the guest's load of the host's world (it was
  in the guest's copy before the load).
* Not verified: the part offered in the Cell editor after a share (the game's own pickup writes the same
  value); the Collect mission counter (1/6) does not count a shared part; two PCs.

## History
- 2026-11-02: opened.
- 2026-11-10: fixed on feat/lineages-45 (parts.cell_shared).

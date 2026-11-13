---
id: BUG-9171
title: Cell: a knocked-out cell watches a partner (SPECTATING, Tab), decision 1W
status: open
kind: feature
stage: cell
reported: 2026-11-13
source: owner
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN Cell host + guest 2026-11-13: coop.pool hurt, goto, spectate, cam)
signature:
docs: docs/re/cell.md#cell-spectate
related: BUG-9156 BUG-0343 BUG-0174 BUG-9118
switch: cell.spectate
---

## Report
Owner decision 1W (2026-11-12): a downed player spectates a partner ("Tab works, but also a left and right arrow next to the name"); built in Creature and Adventure (BUG-9156), not in Cell, Tribe and Space.

## Root cause
Design. cCellUI +0xE0 is the pool index of a cell the camera follows instead of the avatar (the cinematics use it, docs/re/cell.md#cell-camera); a cell's first field is its pool index. A guest's cells come from the host around the guest's view centre, its own cell (BUG-0174), so a camera at a partner far away saw no cells there.

## Fix
cell.spectate (on), cellsync.cpp spectate_tick: while the own cell is knocked out the follow field gets the nearest standing partner's cell (puppet; checked with modes::cell_at each tick), Tab the next one, "SPECTATING <name>" and "Tab: next partner" over the knockout ring; not while the game's own cinematic holds the field; up again: the field 0 and the camera snapped to the own cell. MSG_CELL_KO carries one more byte, the partner watched (older games send none and ignore it); the host makes the partner's position that guest's view centre while it is knocked out, so the cells around the partner come. `coop.pool spectate` (tests).

## Verification
no-fail-run: a feature. Live 2026-11-13, TITAN host + guest on a Cell save: `coop.pool hurt 1 60` on the host knocked the guest out: "spectate ... downed=1 partner=0 'CellHost' camera_follow=<the host puppet's index>", SPECTATING CellHost on screen; the host swam away: the guest's camera at the host (`coop.pool cam`: 28 m from the own cell), the host's cell and the cells around it shown after the view-centre change (6 m: sharp, a big predator and food around); after the knockout "downed=0 ... camera_follow=00000000", the camera 1.2 m from the own cell; 0 faults on both. Seen: farther than about 20 m from the own cell the game draws everything blurred (the partner too); Tab not pressed (one partner). The Creature-style box with the < > arrows and GIVE UP is not in Cell (G still gives up).

## History
- 2026-11-13: opened.

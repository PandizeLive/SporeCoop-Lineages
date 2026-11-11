---
id: BUG-9122
title: Cell: a partner's respawn blacks out this player's screen
status: fixed
kind: bug
stage: Cell
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: all
versions: up to 0.7.8-lineages49-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: QA Cellfive.spo host 9 + guest 8, guest `coop.pool selfhit 20` + `coop.giveup`, host screenshots every 0.3-0.5 s (mean brightness)
signature: -
docs: -
related: BUG-0237
switch: cell.partner_hatch
---

## Cause
0.4 s after a partner's KO_RESPAWNED the partners played "cell_egg_L0_autohatch" on its puppet. That is the own
cell's respawn hatch and includes the screen fade: on the partner's game the whole screen went black.

## Fix
No effect on the partner's puppet (it just appears next to its partner). "cell_hatch_ice" (the revive burst) was
tried: it still dimmed the screen.

## Verification
Host brightness right after the guest's respawn: egg hatch (off) 8 then 89-91; ice hatch 21 then 87; none (on)
79-83 throughout.

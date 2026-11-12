---
id: BUG-9155
title: Party wipe: one founder village wiped out costs the whole party its newest win
status: open
kind: feature
stage: tribe
reported: 2026-11-12
source: owner
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual two-PC run 2026-11-12: coop.tribe win / wipe)
signature:
docs: docs/re/tribe.md#tribe-party-wipe
related: BUG-9147 BUG-9135 BUG-9118
switch: tribe.party_wipe
---

## Report
Owner decision 1U, changed 2026-11-12: "a players whole tribe causes the loss of progression for both players, encouraging rescues". Before, a founder village whose last villager was not revived was captured by the attacker (its partner had to rebuild it by hand, the Village page's Rebuild) or lost 10% food to wildlife.

## Root cause
Design. The campaign's wins are the stage's goal history (TribeUI +0x40 / +0x44, records of 16 bytes {vt, kind 1 conquered / 2 allied, cTribe*, 0}) and cPlayer +0x10F0; the totem's pieces are its kind and colour vectors (+0x11C, +0x130). No native call removes a win or a totem piece.

## Fix
tribe.party_wipe (on). At the end of a founder's window without a revive (or a give-up), party_wipe: the newest goal record goes, +0x10F0 drops by one, the top piece goes from every founder village's totem with that many pieces, an ally's relation to every founder village is set to -5 (a rival); with no win, every present village loses 10% food. No capture: the villager gets up at its village at 40%, the world is saved and reloaded (checkpoint_reload) so the totem and the bar are redrawn, and the guests get the world as after any campaign change. Rebuilding is no longer needed after a wipe. Tests: `coop.tribe win <faction>` (an alliance as a native win: TribeStage_AddGoalProgress, Totem_AddSegment, relations raised), `coop.tribe wipe <seat>`, `coop.tribe wins`.

## Verification
no-fail-run (feature, manual two-PC run). 2026-11-12, TITAN host + MSI guest on a fresh copy of the owner's Tribe save: `coop.tribe win 000021DA` gave 1 win (allied 000021DA, progress 1.0 / 5.0); `coop.tribe wipe 1`: "MSI's village was wiped out: its newest ally (000021DA) is a rival again, a totem piece and a step of progress are lost.", then 0 wins, progress 0.0 / 5.0, relation 000021DA to both founder villages -5.0, the world reloaded, 0 faults. Not seen live: the wipe from a real rescue window, the 10% food branch, a conquest as the newest win, the red progress-debt marker (progress_debt has no Tribe bar yet), 3-4 players.

## History
- 2026-11-12: opened.

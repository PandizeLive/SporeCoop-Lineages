---
id: BUG-9124
title: Creature: bones a partner used stay visible here and cannot be used
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: host
versions: 0.7.4 to 0.7.8-lineages49-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8, `coop.wobj use` on the guest's bone 4750
signature: -
docs: docs/re/creature.md#creature-world-objects
related: BUG-0459 BUG-0473
switch: wobj.ornament_model
---

## Report
"When bones are picked up by the connected player, the host still sees the bones, but can no longer pick them up."

## Findings
* The "cannot pick them up" part is BUG-0459's fix working: a partner's use marks the reward used here (no second
  part).
* The interact node also calls cInteractiveOrnament_NextModel 0xC40360 (the next model in the ornament's list);
  worldobj's orn_use did not. Added (switch wobj.ornament_model).
* Not confirmed: Samuth.spo's bones (4750 and its neighbours) have no model list (+0x248 = -1, list empty), so
  NextModel changes nothing there, and the reward has no effect (+0x1C = 0). What makes a used bone look
  different on the picker's screen is still unknown; the QA screenshots near bone 4750 were not conclusive.
* Next: the owner's save and the bone where it was seen.

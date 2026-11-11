---
id: BUG-9125
title: Cell: meat a partner ate stays on this screen at a huge size
status: open
kind: bug
stage: Cell
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: host (possibly guest)
versions: 0.7.8-lineages49-dev
fixed_in:
branch:
commits:
tests: -
signature: -
docs: -
related: BUG-0364
switch: -
---

## Report
"When meat is eaten by the connected player (possibly vice versa), the host sees it still but massive size."

## Findings (not reproduced)
QA Cellfive.spo host 9 + guest 8: the guest ate ambient meat c2272067774 (`coop.pool swimto` + `coop.pool eat`,
12 points): gone on both games within 1 s. The host's own eat of another chunk: gone on both. Not tried: meat from
a kill (loot), a chunk bitten off a big cell (EatChunkA/B, MSG_CELL_HIT type 6), meat bigger than the eater.

---
id: BUG-9153
title: Village page removed: guests play their village with the game's own controls
status: open
kind: feature
stage: tribe
reported: 2026-11-12
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual two-PC run 2026-11-12)
signature: MSI crash 20261112-140751: read of 0x154 at Tribe_HutStateIs2 (C90730) +C via the getter 8E8230
docs: docs/re/tribe.md#tribe-guest-own-player-village
related: BUG-9152 BUG-9135
switch: ui.village_page
---

## Report
Owner (2026-11-12, decision 2L): "B - prioritize this": the Co-op window's "Village >" page goes now; a guest plays their own village with the game's own controls. QA on the MSI (guest): the guest's HUD shows the host's village as its own (the food counter, the villager cards on the right, the camera's home), so the hut's Add Baby, the tool menu and Rebuild only existed as Village page buttons.

## Root cause
Spore has one player tribe per game (noun manager +0x70); on a guest's game it is the host's village and the guest's own village is an NPC tribe there. Every native player UI (food counter, member cards, hut panel, tool planner, camera home) reads the player tribe. Observed 2026-11-12 on the MSI: with SetPlayerTribe pointed at the guest's own village (`coop.tribe perspective own`) the food counter and the cards follow it and a card click selects the guest's chief; the camera stays at the host's village. The first try crashed about 15 s later: the Tribe UI's ring code (TribalGame_HandleMessage message type 3 -> CD5190 -> CCC580 -> CC95E0) called Tribe_HutStateIs2 (C90730) for the owner of a citizen whose hut (+0x368) was null; C90730 has no null check. Which tribe had no hut is not known (both villages had one).

## Fix
Step 1 (this commit): the Village page is hidden (`coop.fix ui.village_page on`: back). Its console commands stay for tests. Until the native versions come, a guest cannot add a baby, build a tool or rebuild a partner's village (owner accepted that with 2L B). Tribe_HutStateIs2 gets a null-hut guard (false). The experiment `coop.tribe perspective own|host|status` swaps a guest's player tribe; player_tribe() keeps meaning the host's village for the mod while swapped. Next: the swap as the guest's normal state (join, stage and world changes, leave), the camera home, the hut's Add Baby and the tool planner sent to the host (CMD_BABY / CMD_BUILD exist), Rebuild automatic with the party wipe (decision 1U).

## Verification
no-fail-run: feature in progress, manual two-PC run. 2026-11-12, TITAN host + MSI guest on a fresh copy of the owner's Tribe save: the swap without the guard crashed the MSI after about 15 s (crash-20261112-140751); with the guard it ran over 6 minutes with 0 faults (hutless reads 0: the trigger did not come again). The Village button is gone from the Co-op window.

## History
- 2026-11-12: opened.

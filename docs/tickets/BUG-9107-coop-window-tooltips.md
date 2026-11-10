---
id: BUG-9107
title: Co-op window: buttons have no tooltips
status: open
kind: feature
stage: All
reported: 2026-11-10
source: player request (roadmap 42)
role: all
versions: 0.7.3.6-lineages41-dev
fixed_in:
branch: feat/lineages-42
commits:
tests: QA hover screenshots, see Verification
signature: -
docs: -
related: BUG-9105
---

## Request

"Apply tooltips to the majority of the buttons in our menu: very short, a couple of words."

## Implementation (mods/coop/coop_ui.cpp, mouse.cpp)

- `tip_for(name)`: two to five words (English and Russian) for every co-op window button by its
  name (Connect, Lobby and Space pages, the eye buttons, server rows); the Space page's action
  buttons change meaning per row and have none.
- mouse.cpp keeps the last WM_MOUSEMOVE over the game window (client pixels, `mouse::last_move`).
- `draw_tooltip()` (SK_EV_OVERLAY, coop.cpp `on_overlay`): when the window is open and the mouse
  has rested 0.5 s on a visible button with a tip, a box in the window's gold edge and dark panel
  blue is drawn next to the cursor (kept on screen) with the overlay font. Nothing is added to the
  Spore UI, so there are no new windows to manage. `coop.fix ui.tooltips off` hides them.
- Spore's own tooltip window procedure (UTFWin::SporeTooltipWinProc, march2017 sym) was not used:
  it needs a serializer and proxy IDs that are not mapped yet.

## Verification

QA, second TITAN instance (LineagesDiff41Guest), Connect page at 800x600: Refresh "Find LAN
games", eye "Show or hide", Fast "Fastest world transfer", Start Server "Host a new session"
(near the right edge, kept on screen). 0 faults.

## History
- 2026-11-10: built on feat/lineages-42; QA above.

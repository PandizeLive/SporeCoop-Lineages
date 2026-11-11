---
id: BUG-9130
title: Creature: Ctrl+Scroll changes the FoV steps but the picture does not change
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev ("FoV ... isn't functioning or adding camera FOV")
role: local
versions: 0.7.3.6-lineages41-dev and later (at least)
fixed_in:
branch:
commits:
tests: QA Samuth.spo host 9: `coop.fov 0` / `coop.fov 6`, screenshots
signature: coop.fov "steps=6 vanilla=44.56 effective=62.56", the frame identical to steps=0
docs: -
related: BUG-9099
switch: -
---

## Findings
* The player's own log: Ctrl+Scroll reaches the mod (fov: steps=1 .. 6).
* QA: steps 6 -> effective lens 62.56 (vanilla 44.56) inside fov.cpp's begin_view, but the screenshots at steps 0 and
  6 are the same frame (same trees and horizon). The projection set through the view's perspective setter during
  BeginUpdate is not the one the scene is drawn with (another pass or a later camera update rebuilds it). Needs the
  camera pipeline (cCameraManager::Update, the follow camera 0xB03AF999's own field of view) looked at.

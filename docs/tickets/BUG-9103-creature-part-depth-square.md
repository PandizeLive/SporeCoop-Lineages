---
id: BUG-9103
title: Creature editor: a depth square pushes details and weapons into the body or pulls them out
status: open
kind: feature
stage: Creature
reported: 2026-11-09
source: player request (roadmap 42)
role: all
versions: 0.7.3.6-lineages41-dev
fixed_in:
branch: feat/lineages-42
commits:
tests: QA coop.depth (single PC: drag, save, reload, undo); two-PC TITAN host + MSI guest, see Verification
signature: -
docs: docs/re/creations.md#creations-editor-depth-handle
related:
---

## Request

"A depth square (native from editors with vehicles and others) for creature parts", a
square that glows orange when grabbed, in a similar place to the native one. Limbs,
hands and feet keep their placement; weapons and details move.

## Findings

- Spore's own depth handle is the "nudge" deformation handle (kind fnv("nudge")
  0x503283AA). Only the Adventure outfitter setup names a nudge tuning file, and that
  file (0x10CB4B28!0x3C3BA77F) is in no game package: a cut or unused feature. The
  handle comes from deformations in accessory models; creature part models have none.
- Appending a nudge definition to IModelWorld::GetDeformationHandles (vt 0xE8,
  0x745000) crashed the editor's handle build (per-handle arrays at rigblock +0x704 and
  +0x73C are sized from the model). Abandoned.
- Moving a part: write EditorRigblock +0x48 and its model's offset (model +0x0C), set
  EditorModel +0x51 (changed) and call cEditor::CommitEditHistory(true, 0): the editor
  then offers Save/Replace, Undo restores the part, and the saved model (bem) keeps the
  position; reopening the creation keeps it (no re-snapping on load).

## Implementation (mods/coop/editor_depth.cpp)

- In the creature editors (EditorModel type 0x9EA3031A), the selected part gets a square
  drawn on the overlay, 0.18 x part scale outside the part along "out of the body"
  (from the nearest spine segment through the part). Spine (modelIsVertebra), limbs
  (ball/socket connector, capability limb), hands and feet get none.
- Colours from Spore's editor handle tuning: pale blue (0.67, 0.75, 1.2), green on
  rollover; orange (the power handle's colour) while held.
- The handle is drawn as a small lit cube (corners projected with the game camera, no outline,
  shading bent toward the corners, tones matched to the editor's handle balls); `coop.fix
  editor.depth_cube off` draws the flat square instead. No handle while a dialog is the window
  manager's modal window (+0x82C): before this the square showed over the exit box.
- Press on the square: the editor does not see the press (the part stays selected); moving
  the mouse moves the part along its outward line. Limits come from the body, not the part
  size: the part's distance from the nearest spine segment when the editor first saw it is the
  body depth there; it goes out at most 0.3 x that (and 0.2 x part scale), and in no closer to
  the spine than 0.25 x that, so it never comes out the other side. Drags do not add up (the
  first-seen depth is kept per part while the editor model is open);
  the mirrored twin (x mirrored, any part id: left and right ears differ) moves mirrored;
  release commits an undo step and marks the creation changed.
- `coop.fix editor.depth` (off = no square); `coop.depth [parts | open <creature>]` (tests).

## Verification

Single PC (QA copy, profile LineagesDepthQA, Creature save Samuth, 640x480, muted):
- ear 05A9FD99 square at (560,293); hover green, drag orange, release pale blue
  (out/sq-strip.png); position 0.089 -> 0.239 along out (0.908 -0.395 -0.143),
  twin 17A60A7F mirrored; 1 commit; 0 faults.
- Save New -> creation 291C7405: bem stores 05A9FD99 (0.239, -0.300, 0.820) and
  17A60A7F (-0.239, -0.300, 0.820). `coop.depth open 291C7405` reopens it with both ears
  there. A second drag then the editor's Undo restored 0.239. 0 faults.
- Two PCs: see History.

## History
- 2026-11-09: built on feat/lineages-42; single-PC QA above.
- 2026-11-10: two PCs (TITAN host, MSI guest over Tailscale, both QA instances, muted): the
  MSI joined, TITAN dragged the ear (0.090 -> 0.241, twin mirrored), Save New, 0 faults on
  both. The partner's "player 0 creature" (7CE5A798) arrived byte-identical on the MSI (bem
  hash E85A00D6 on both), but it is the host's unchanged avatar body (same hash as 2919A819):
  in this QA world the avatar species stays 2919A819 after a Save New from the editor vote, so
  co-op does not send the edited creation. Only the avatar travels today; sending saved
  creations to partners is the Lineages Cache work of roadmap 42 (1A). The depth itself is in
  the bem (a byte-identical transfer carries it).
- 2026-11-10: depth kept in a new creation, checked by opening it in a fresh editor:
  291D1C46 beak C8F7A86A -0.351 -> -0.438 kept. Test pitfall: `coop.depth open` while an
  editor is already open does not switch creations and stacks editors (letterboxed UI); open
  only from the stage.
- 2026-11-10: one R6025 (pure virtual function call) after Save New, once, in a run where the
  test set the editor's selection by writing cEditor +0xD4 (bypassing the editor's own select)
  and pushed the ear 0.47 out. Not seen with selection by click: three Save New runs, one at
  the old maximum (beak -0.351 -> -0.689), 0 faults. The new limits keep parts far nearer.
- 2026-11-10: cube handle, dialog hiding and body limits (beak range -0.124..+0.050 at body
  depth 0.166; a second drag out gets 0), 0 faults.

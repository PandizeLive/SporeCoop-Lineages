---
id: BUG-0177
title: Creature: the mating call did not lead to the editor in the bot
status: open
kind: test
stage: Creature
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Backspace (Mating Call) at the nest with DNA shows "Mate: Locate your Mate!" and a thought bubble,
  but clicking the family members around did not open the editor within 30 s. Not a confirmed bug:
  how a player answers the call is not explored yet (docs/re/creature.md 5). The evolution vote is
  covered by `coop_vote.py` through `coop.vote editor`. The Cell stage's mating call is driven for
  real since 2026-10-24 (`coop_editor_desync.py`, docs/re/cell.md 12), and the Creature stage's first
  editor through the stage switch, not the creature mating call.

## Root cause
A hosted 37-dev reproduction in a disposable copy of the reported campaign
showed the Mating Call animation and nest marker, with no heart marker over a
nestmate. Clicking a nearby nestmate reached the avatar's native mating
behavior (Steam/GOG 2024 code, preferred address 0x00D6D560). Runtime sampling
showed phase 1 with a resolved animal target. The target stayed in action
0x71BE644C instead of entering the reply action 0x91BFFC22 that advances the
native behavior to phase 2. The egg-laying cinematic did not start and the
game stayed in Creature. A separate run with Co-op unloaded reached phase 2, but the test ended before confirming editor entry. The two runs did not establish that they selected an equivalent nestmate.

In a later same-save comparison the native click classifier found an eligible
mate in upstream 0.7.8 and both Lineages builds. Dispatching that action opened
the Creature editor on upstream 0.7.8 and in a separate 37-dev run. In the
30-dev comparison the native action stayed in Creature without starting the
egg cinematic. This establishes intermittent failure before editor submission
and does not identify a regression commit or explain the missing local cue.

## Fix
38-dev shipped a temporary clickable Mate marker. The next patch removes it
and starts Spore's own pooled heart effect when a living, eligible nestmate
from the local player's herd lacks it. A runtime check confirmed that the
eligible mate in the report had no instance of effect 0x9005E011.

A separate response trace caught a stalled click with the player 1.61 m from
the nest and the selected mate 11.30 m away. The native response radius was
9.67 m, so the mate kept scoring zero. The patch allows up to five additional
metres of wandering for the selected, native-eligible nestmate while the
player is within 0.25 m of the original radius and performing the native mating action.
A separate run reached phase 2, substep 1, but the mate resumed roaming and
Spore kept waiting for the paired movement to finish. The patch can repeat
Spore's staging step twice, four seconds apart, if either participant is
still waiting to reach its position.

The native approach, egg cinematic, editor request and multiplayer vote still
handle the sequence.

## Verification

An isolated Steam host loaded a copy of the affected save. It displayed
Spore's mate hearts, played exactly one egg cinematic after the native
click handler, and entered Editor mode with zero logged faults. A second
run moved the mate to 10.88 m from the nest before dispatch, beyond the
9.67 m baseline, and also entered the editor with zero logged faults.
The final build passed a normal mouse click in the same save. The log
recorded both paired-approach retries, one egg cinematic, and zero faults,
and the Creature editor rendered. Guest mating was outside this reproduction.


## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: how a player answers the Creature mating call (which object to click after Backspace) is unexplored reverse engineering. Left open.
- 2026-11-09: reproduced the native mating behavior waiting for its partner in a 37-dev hosted QA copy.

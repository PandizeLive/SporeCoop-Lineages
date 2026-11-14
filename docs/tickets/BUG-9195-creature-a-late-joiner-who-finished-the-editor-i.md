---
id: BUG-9195
title: Creature: a late joiner who finished the editor is sent to the galaxy menu and gets the world again
status: fixed
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits:
tests: tools/qa_win (to run: a joiner's first creature)
signature:
docs:
related: BUG-9128 BUG-9167 BUG-9193
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "When MSIJoiner finished creating their creature, they were spawning in on Host's nest, then got sent to the galaxy menu before being resent the world and rejoining and being at the Host's nest instead of their own still."

## Root cause
Observed: by design a new nest reaches a guest only with a fresh world (home_nests.cpp: a nest made on a guest crashed, BUG-9128): host 20:39:39 "seat 2's world goes out 4 s after its nest was made", joiner 20:39:52 the galaxy menu and the reload. The spot after it: BUG-9193 (1). MSIGuest loaded twice too: its join share was saved before its nest existed.

## Fix
A notice "<name>'s own nest is ready: their world reloads once to bring it" when that world goes out; the spot is BUG-9193's fix. Open: making a joiner's nest before its first share (one load instead of two).

## Verification
no-fail-run: a notice; the reload stays by design.

## History
- 2026-11-13: opened.

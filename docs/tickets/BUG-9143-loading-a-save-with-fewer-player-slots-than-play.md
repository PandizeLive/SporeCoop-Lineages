---
id: BUG-9143
title: loading a save with fewer player slots than players connected ends the whole session
status: open
kind: bug
stage: all
reported: 2026-11-12
source: found in QA 2026-11-12
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch:
commits:
tests:
signature: "This saved campaign has fewer reserved founders than the current lobby. Rehost from this planet."
docs:
related: BUG-9135
---

## Report
QA 2026-11-12: four players connected; the host loaded another save (Vandh, 3 player slots): the whole session closed for everybody ("fewer reserved founders than the current lobby").

## Root cause
world.cpp apply_saved_rules refuses a saved campaign whose slots are fewer than the connected players and the host leaves the session. With slots added mid-stage (BUG-9135, `coop.seats N now`) the host could raise the loaded campaign's slots instead (difficulty up), or ask.

## Fix
Not fixed. The message now reads "This save has fewer player slots than players connected. Rehost from this planet." (BUG-9144). Owner decision needed: raise the slots of the loaded save automatically, ask the host, or keep.

## Verification
no-fail-run: not fixed yet.

## History
- 2026-11-12: opened.

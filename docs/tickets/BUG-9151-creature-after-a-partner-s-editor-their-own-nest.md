---
id: BUG-9151
title: creature: after a partner's editor their own nest is rebuilt for the new species and the old one stays
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: found by code reading 2026-11-12 (personal editor research)
role: all
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Found by code reading (not run): after a partner leaves the Creature editor with a new species, their own nest (roadmap phase 2, creature.own_nests) may be rebuilt for the new species, the whole world resent to them, and the old nest left behind as an AI nest of the old body; the host's copy of the family keeps the old look.

## Root cause
Inferred: a seat's species follows the partner's new key (party.cpp, around 114-118); home_nests then finds no nest of the new species and builds one (home_nests.cpp, around 204-245). To verify in a game before the personal editor (ROADMAP, "a personal editor with the native cocoon") is built: the nest should change species in place.

## Fix
Not fixed.

## Verification
no-fail-run: not reproduced yet.

## History
- 2026-11-12: opened.

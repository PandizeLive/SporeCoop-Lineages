---
id: BUG-0455
title: a player's game crashes at 0xE83EC8 in the Cell editor when the host leaves the evolution (editor) screen, 0.7.3.4 (BUG-0004 signature again)
status: open
kind: crash
stage: Cell
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature: 3010a413:0xE83EC8 read 0x9C (Cell puppet creation after editor return)
docs:
related:
---

## Report
Another player (forum post 'Cell Editor 0.7.3.4 Crash', with a video): crash dump crash-20261102-144335.dmp: 0xC0000005 at SporeApp.exe+0xA83EC8 (base 0xD90000) = dc04aee5 0xE83EC8, read of 0x9C; 'Game crashed when host leave evolution tab'. 0xE83EC8 is BUG-0004's signature (editor desync, guest crash, fixed long ago): a sibling path or regression. No report zip (the player could not send it); the .txt was only a Discord preview.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

Lineages reproduced the same fault on the host when a guest saved a free
rename of an imported cell. The received DBPF contained only a metadata record.
Registering it under a remapped key left that key without a model when native
Cell puppet creation tried to resolve it. The guest's local EditorSaves had
the new metadata and the original model was already in a mounted package.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

For a metadata-only Cell save, combine the new metadata with the unchanged
model from the mounted source package. If the model cannot be read, wait to
send the bundle. A receiver leaves its existing appearance in place when
an older sender sends a partial Cell bundle.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

Two-PC Steam/Tailscale regression in disposable profiles. Before the fix, a
real guest editor rename produced one metadata record and the host faulted
at rebased `0xE83EC8`. After the fix, the host received two consecutive fresh
guest renames with two records each and recorded zero native faults. Each edit
preserved the guest's 5 DNA and current world. The intermediate build changed
back and forth between older names after joining. Reusing the cached model
for catalog lookup removed the repeated switching in the final sample.

## History
- 2026-11-02: opened.
- 2026-11-08: reproduced the metadata-only variant on a Steam Lineages 29 development candidate.

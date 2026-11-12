---
id: BUG-9140
title: tools/tickets.py new fails on Windows: it imports fcntl
status: open
kind: tooling
stage: All
reported: 2026-11-12
source: phase4-tribe agent on the Windows PC (BUG-9135)
role: host
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
`py tools/tickets.py new "..."` on the Windows PC stops with `ModuleNotFoundError: No module named 'fcntl'`
(tickets.py cmd_new, `import fcntl`). The other subcommands (find, check, index) work.

## Root cause
cmd_new locks `<git common dir>/tickets.lock` with fcntl.flock so parallel worktrees never take the same id; fcntl
exists only on POSIX.

## Fix
Not fixed. Proposal: fall back to msvcrt.locking on the same lock file on Windows. Workaround used on phase4-tribe: a
stand-in `fcntl` module on PYTHONPATH whose flock takes an msvcrt byte lock (BUG-9138 and this ticket were made so).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.

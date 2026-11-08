---
id: BUG-0405
title: world transfer to a guest that loads: 50-90 % of the packets are resent on a clean link
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: rejoin_race_long, lab 20261101-134020 (feat/w33-tests-session)
role: host
versions: main a05d1f6
fixed_in: unreleased
branch: fix/lineages-player-session
commits: 75b3901b
tests: tests/world_transfer_tests.cpp; tests/net_tests.cpp
signature:
docs:
related:
---

## Report
`rejoin_race_long` (coop_rejoin_loop.py --rounds 40 --race 1:9, Tribe fixture, lane 9, clean loopback network,
lab 20261101-134020): the invariant monitor's `link` check fired twice on the host, both during a world share to
the rejoining guest: `partner 1 ... resends=4673/6838 ... pending=602` (s 353, share started s 350.4, "transfer
to 1 queued completely (3432 KB in 10.6 s, 324 KB/s, rtt 81 ms)"), and again at s 463 (resends/sent 0.51 and
0.92 between trace lines). The host's per-minute net line: `rtt 18/219/1407 ms ... resends 1323, backlog 160
(max 310)`. The same transfer without a load on the guest side ran at 2603 KB/s (chaos_civ_long guest log).
The guest's frames stall while it loads (fps avg 5-9, hitches 200-950 ms in the same minutes).

How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_rejoin_loop.py --rounds 40 --race 1:9`, then
`grep 'partner 1' sporekit.<host>.log | grep resends` around each `world: sharing`.

## Root cause
Not analysed. Inferred: the guest acknowledges on its frame (a stalled frame delays the acks by up to a
second), the host's resend timer follows the rtt estimate (avg 219 ms, max 1407 ms), so the reliable chunks
of the world are sent again while their acks wait in a stalled guest. On a real link with a bandwidth cap
this doubles the transfer's traffic in the moment it matters most (the join). Not a desync: the transfer
completed every time (the monitor's link check now leaves world transfers out, tools/tests/monitor.py
`Timeline.sharing`).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
2026-11-07: FAIL in out/player-report-20261107-223741/transfer-before.log;
PASS in transfer-after.log and transfer-repeat.log in that same folder.
Full net-regression.log is ALL OK. Native delayed-rejoin-final.log passes
against the final Lineages 28 binary. See selective-port section below.

<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: Lineages 27 player report has a 2.7 MiB transfer taking 539.8 seconds
  to queue over a roughly 130 ms RTT link, with heavy retransmissions. The fork
  lacks upstream bulk_acks. Selective port tracked with BUG-9096.
- 2026-11-01: opened.

## Verified selective port (2026-11-07)

CORRECTION: the older unanalysed root-cause text above is historical. Upstream
v0.7.9 connection.cpp/session.cpp provides the validated implementation: urgent
acknowledgements while draining receive bursts, and retransmission only with
newer acknowledged traffic or a longer timeout. Guard: net.bulk_acks. Wire format
unchanged. See docs/re/cell.md#cell-lineages-player-report.

Verification: out/player-report-20261107-223741/transfer-before.log is FAIL;
transfer-after.log and transfer-repeat.log are PASS (five cases). Full Windows
net-regression.log is ALL OK. Native delayed-rejoin.log completed in 13.75 s;
the final versioned binary completed in 19.78 s (delayed-rejoin-final.log), both
with loopback 60 ms each way and deterministic 2% loss. No claim of the same
throughput on the player's physical connection.

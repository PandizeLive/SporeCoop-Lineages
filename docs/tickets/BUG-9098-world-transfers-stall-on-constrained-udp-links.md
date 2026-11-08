---
id: BUG-9098
title: World transfers stall on constrained UDP links while Cell snapshots fill the path
status: open
kind: bug
stage: Cell
reported: 2026-11-08
source: separate-network two-PC Lineages QA
role: host and guest
versions: 0.7.3.6-lineages28-dev
fixed_in:
branch: fix/lineages-player-session
commits:
tests: world_transfer_tests, two-PC separate-network Cell join
signature: host remains at approximately 160 pending messages while guest waits at Galaxy menu
docs: docs/LINEAGES-INSTALL.md
related: BUG-0405
---

## Report

The same approximately 3.4 MB test world transferred automatically in 6.5 s on
the fast link. The separate-network join completed its handshake, but delivered
only 40 KB after 54 s. A reconnect offered another full transfer and also stalled.

## Findings

A synthetic UDP probe with Spore closed delivered all 80 small datagrams, 27 of
80 game-sized datagrams spaced 40 ms apart, and 4 of 80 game-sized datagrams in
a burst. This establishes loss outside the game on the tested path. It does not
identify the tunnel or network segment that dropped the packets.

The normal host also sends live Cell snapshots to the guest before the guest
has joined that Cell world. The guest cannot use those snapshots in the Galaxy
menu. With a Paced-only host, MSI reached 4% after 60 s, while the host still
sent roughly 270 datagrams per second and its loss estimator reported over 90%.
With the same pacing and a guest-stage gate, MSI reached 8–9% after 60 s and the
host sent roughly 22 datagrams per second.

## Candidate

Normal remains the default. The opt-in Paced mode sends smaller world chunks,
limits the number queued and spaces original sends and retries. The host skips
live Cell snapshots until the guest reports being in the same Cell world.

## Verification

The portable network suite and the focused transfer suite passed. The real
separate-network gated transfer used a 28-dev guest. It received 3,499,337 bytes
in 11 min 25.5 s, entered playable Cell 11 min 28.7 s after joining, rendered
the host avatar and showed 23 mirrored cells. The download remains slow on
this link, so the ticket stays open.

Normal with the same snapshot gate reached about 7% after 60 s, versus 8–9%
in Paced. Normal still accumulated more than 27,000 reliable-message resends
by the end of the short run. The host reported zero native faults. Both test
processes closed normally. A previous Normal attempt was blocked before the
offer by the native save confirmation (BUG-9057) and is not a speed result.

## History

- 2026-11-08: reproduced on two PCs and added a pacing and snapshot-gating candidate.

## TCP check on the same separate-network path

Two HTTP/TCP requests for a 3,461,524-byte uncompressed QA save archive
received their headers in 318–333 ms. Both timed out at 55 s with zero body
bytes on the guest. The sender's writes were accepted into its socket buffer;
that does not prove receipt. Spore was closed on both PCs. This result does
not support changing world transfers to TCP as a remedy for this path.
The follow-up used the same 16,384 synthetic bytes in both requests. The
256-byte writes spaced 30 ms apart completed in 2.238 s with a matching
SHA-256. One bulk write returned headers but delivered zero body bytes in 12 s.
This isolates a size or burst sensitivity on the tested path even under TCP.
Because the probe changed both write size and spacing, it does not establish
the effective path MTU or the exact loss mechanism.

# Windows QA and two-PC tests

Tools for testing on the owner's Windows PC (the QA copy of the game) and the MSI test PC over Tailscale. The Linux
lab (`tools/lab.py`, Wine lanes) stays the main test runner; these cover what needs a real Windows game or two PCs.
Background and pitfalls: docs/06-testing.md#testing-two-pc-msi.

| Tool | What it does |
|---|---|
| `qa.py <instance or msi> <command>` | One console command to a QA game on this PC, or to the MSI through the tunnel. Refuses a port that is not a QA game / the tunnel. |
| `launch.ps1 -Instance N -Profile P [-Resolution WxH]` | Starts a QA game (800x600 by default; 5 FPS from the QA copy's sporekit.ini). |
| `msi.ps1 deploy / start / stop / tunnel` | Copies the build to the MSI, starts or stops Spore there, opens the console tunnel. |
| `respawn_fade.py <dier> <watcher>...` | Cell: a respawn must not darken the other screens (BUG-9122). |
| `social_hits.py msi <host> <npc>...` | Creature: befriending by real input on the MSI never hurts the creature on the host (BUG-9136). |

Rules (owner, 2026-11-11 / 2026-11-12):

* Never the owner's own install or profile: QA copies and test profiles only (`SporeKit-<profile>`); a save of theirs
  is copied into a test profile (read-only on their files).
* Console `click`, `drag`, `wheel`, `key` move the real mouse and keyboard: on this PC only with the owner's OK; on the
  MSI freely.
* Up to 3 test games on this PC at 5 FPS in small windows, unless a test needs a size (Adventure clicks: 1440x1080).
* A header change needs no FORCE=1 any more (tools/build_windows.sh tracks includes since 2026-11-12), but the MSI
  and the QA copy always get the whole new build.

A typical two-PC session:

```
pwsh tools/qa_win/launch.ps1 -Instance 9 -Profile DuoQAJiffHost
pwsh tools/qa_win/msi.ps1 deploy
pwsh tools/qa_win/msi.ps1 start
pwsh tools/qa_win/msi.ps1 tunnel        # leave it running
py tools/qa_win/qa.py 9 coop.host 47839 Host 4
py tools/qa_win/qa.py msi coop.join <this PC's Tailscale address>:47839 MSI
```

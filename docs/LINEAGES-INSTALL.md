# SporeCoop Lineages: private Windows preview

Requires Spore with Galactic Adventures on supported Steam or GOG 2024 builds.
Every player must use the same Lineages version, gameplay mods and Creepy & Cute
content configuration. Start with two players and a separate test campaign.
Keep a pre-mod save backup for vanilla play.

## Install

1. Close Spore. Extract the entire ZIP into a folder you can keep.
2. Keep SporeCoopSetup.exe and the included .pack file together.
3. Open SporeCoopSetup.exe, select the Galactic Adventures installation and
   click Install. If Windows denies access, run setup as administrator.
4. Launch Galactic Adventures normally. Select Co-op in the galaxy menu or F9.

## Join friends

One player selects Host, then loads or starts the test campaign. Friends select
Join and enter the host's reachable address. The default game port is UDP 47810.
For your existing Tailscale or Hamachi setup, use the host's VPN address and
ensure the PCs can reach each other. The VPN is configured separately.
Allow the game through Windows Firewall when prompted; do not disable it.

If a guest connects but the world download stalls, switch **Transfer** from
**Normal** to **Paced** on the host's Co-op connection screen before hosting.
Paced uses smaller packets and spaces out retries. It can take several minutes
for a full world, so keep **Normal** for links that already transfer reliably.
The setting is saved on the host and guests can use the same join steps.

In Creature and Adventure third person, hold **Shift** and scroll to widen
the camera by up to 18 degrees. Scroll back while holding Shift to restore
the vanilla field of view. This is local to your game on Steam and GOG 2024.

## Uninstall

Close Spore, open the same setup and click Uninstall. The installer restores
the original Steam API loader and preserves saves, settings and logs. Use your
pre-mod backup for vanilla play; custom-founder campaigns can depend on the mod.

## Lineages Cache

Cache-enabled builds automatically exchange locally authored creations with
connected friends in the background. In Sporepedia, open **Lineages Cache >
Shared Creations**. Creatures, buildings, vehicles and ships retain their embedded
names and authors. The co-op display name is recorded separately as the sharer.
The host relays guests' creations to other connected guests. Large libraries take
time to finish; imports wait while an editor or loading cinematic is active.

Received creations persist in that Spore profile's native library across solo
and co-op games, without an EA login. They are available to the game's normal
selection system; the cache does not force them to spawn or alter existing worlds.
Use matching installed content/mods. Arbitrary missing-part combinations and
Adventure/flora libraries have not been qualified.

Uninstall removes the mod's category, but retains imported native creations and
the profile's SporeCoop/LineagesCache files. Deleting only the cache folder does
not remove creations already imported into Spore. Manage those through the
normal Sporepedia; do not delete Pollination.package or campaign saves.

## Preview qualification

This package includes selected upstream 0.7.9 stability fixes and the current
Lineages campaign code. Targeted two-instance Tribal and Creature checks passed.
Separate-PC Tailscale checks covered Cell world transfer, unchanged Cell
reconnect, and automatic loading of a prepared Creature world after reconnect.
An uninterrupted Cell-to-Space run, broad third-party mod compatibility and
three/four-player campaigns are not fully qualified.
Adventures are incomplete: shared combat, partner bodies and final rewards need
further work. Use this as a development preview, not a finished release.

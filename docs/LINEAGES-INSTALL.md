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

## Uninstall

Close Spore, open the same setup and click Uninstall. The installer restores
the original Steam API loader and preserves saves, settings and logs. Use your
pre-mod backup for vanilla play; custom-founder campaigns can depend on the mod.

## Preview limits

This package includes selected upstream 0.7.9 stability fixes and the current
Lineages campaign code. Targeted two-instance Tribal and Creature checks passed.
An uninterrupted Cell-to-Space run, separate-PC VPN testing, broad third-party
mod compatibility and three/four-player campaigns are not fully qualified.
Adventures are incomplete: shared combat, partner bodies and final rewards need
further work. Use this as a development preview, not a finished release.

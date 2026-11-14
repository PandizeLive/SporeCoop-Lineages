---
id: BUG-9191
title: UI: the Party panel (the owner's mockup) replaces the CO-OP player list; a portrait click opens the player's creature in the Sporepedia; a UI scale setting
status: open
kind: feature
stage: all
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + guest + the MSI, 2026-11-13)
signature:
docs:
related: BUG-9135 BUG-9171
switch: ui.party_panel ui.party_portraits
---

## Report
Owner 2026-11-13 (screenshot with the CO-OP list crossed out, images/partymockup.psd): "Let's get rid of this eyesore of UI and create this new one I made a mockup of. Allow the player to click the connected player's portrait and see their creature in the Sporepedia." "I tried to recreate the Pack UI to an extent with some added features like the names and locations." "Also add a UI scale setting into the options menu next to the borderless mode + the FPS thing." Then: "The healthbar does not look native at all. Review the native Pack UI and see what you can pull from that."

## Root cause
A feature. Findings on the way: the mockup PSD's layer pixels are compressed in a way Pillow cannot read (the merged image only); a sui label draws no drawable (the portrait needs a window of the game's image kind, IImageDrawable::AddImageWindow); a 128x128 picture in a StdDrawable is cut in thirds and shows nothing in a 45-pixel card; a creation's shared key is a hash of its whole bundle, so the picture travels apart (in the bundle it would change every key and the saved founder seats). The game's own health bar (Creature HUD layout 5ED10964, Pack panel B40BABD4, slot layouts EFAD77A3 and others, all in Spore_Graphics / PatchData 40464200): the empty pill, a clipping window (flag 0x400) as wide as the health with the full green pill inside, the edge on top. Putting the slot layout EFAD77A3 on screen with every child visible overflowed the stack (the test command coop.layoutshow, two crashes of test game 8, not in the shipped panel).

## Fix
ui.party_panel (on): coop_ui set_party_hud: a "Party" header and per player a name plate (the own one gold-edged) with the name and the place ("[Space]", "[Ship Editor]": PlayerState.editor, appended), a card with the creature's picture and the native-style health bar (tools/party_ui_assets.py draws the images from the mockup's and the native bar's pixels); hidden under the pause menu, the Sporepedia and captions. ui.party_portraits (on): each player sends its creature's Sporepedia picture (kPortrait, relayed by the host, kept for late joiners) under the key every game knows it by (the host that plays its world's species: that species), registered in a group of the mod. A card click: Sporepedia::ShopperRequest::Show with the creature preselected (opens the Sporepedia browser). UI Scale (75-150 %) under Borderless in Spore's Settings, lineages-graphics.ini ui_scale; the panel follows it.

## Verification
no-fail-run: a feature. Live on the MSI (the new package there): the panel with the three players, their pictures and the bars (out/shots/party-panel-msi.png). Open: the click opens the Sporepedia browser but not the creature's own card (also with request +0x2A = 1; a partner's creation is kept out of My Creations); the UI Scale button not looked at live; the TITAN games read the installed SporeCoop.package (older: the text list there).

## History
- 2026-11-13: opened.
- 2026-11-13: panel, pictures, native-style bar and UI scale built on feat/lineages-47; the Sporepedia card still open.
- 2026-11-13 (later): decision 3E C, the partner arrow is a HUD-blue chevron with a white edge and a name plate (ui.partner_chevron); the header and the other plates have the native Pack pill's thin light edge (the owner's note); live on TITAN (the chevron) and the MSI (the edges); the installed package refreshed (backup SporeCoop.package.2026-11-13-party-v1).
- 2026-11-13 (later): the portrait click opens the creature's own Sporepedia card (request +0x13 = 0: the card, spore-decomp AdvancedLaunch); live: Fourth's own captain, the host's Gentmin on a guest, Fourth's Slinkodum on the host (a partner's shared key works). The panel and the arrows hide while the game time stands. Owner's test: the UI Scale scales only the mod's UI (the owner wants Spore's: research, no native setting, a window transform is the route); hover highlight and size missing; image 4 (Pack-style cards) asked.
- 2026-11-14: decision 3I (the owner's layout): Spore's Settings dialog grows; the Graphic page holds an Interface box with the Interface Size slider (a native WinSlider), the Frame Rate field in the Screen Size box (the cache field's look, arrows step 15..360) and Borderless Mode under Full Screen Mode (exclusive); the left panel is empty again (ui.settings_layout). Decision 3H A: the Interface Size scales Spore's own UI (ui.native_scale: a kMsgTransformed proc on each top piece, pivot on the piece's screen side, no piece past 95 % of the screen; the mouse moved to the pieces' unscaled places, windows with their own transform left to the game). Live on the MSI at 125 %: main menu, Options, Settings (slider, arrows, Cancel), a Tribe HUD button, a world click unchanged. Not checked: editors, Space tools, the planner.

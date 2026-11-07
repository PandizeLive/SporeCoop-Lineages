---
id: BUG-9052
title: Automated native Civilization camera tutorial does not acknowledge panning
status: open
kind: bug
stage: Civilization
reported: 2026-11-05
source: Full native graduation test with first-use tutorials enabled
role: host
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
switch:
signature: camera moves through console input but pan tutorial remains paused
docs: docs/re/tribe.md
related: BUG-9046
---

m2-graduation-full-a.log reached native Civilization on both clients with zero
faults, one native capital, retained host species and both founder records.
Its final busy:- assertion timed out at the first-use camera tutorial. Native
tooltips were acknowledged manually. The two-button camera rotation step
advanced; all arrow directions and WASD moved the camera, but the next native
panning instruction remained paused. SporeKit input already overrides
GetKeyState/GetAsyncKeyState. A mod defect versus an automation-input limitation
is not established. No production workaround is justified yet.
For repeatable campaign handoff qualification, fresh disposable profiles may
set their existing OptionTutorialsEnabled preference to0 before launch.
That fixture does not qualify the first-use camera tutorial. User preferences
are untouched. Real-input tutorial behavior remains to be checked separately.
2026-11-07: reproduced the console-input limitation on lineages25 in isolated
LineagesTutorialA. Tutorials remain enabled. Loading the old Civ checkpoint
alone did not reopen the lesson; the preserved five-victory Tribal checkpoint
was loaded into a new unused Game88, followed by normal City Hall and native
religious vehicle editors. The initial economic-role assertion was a fixture
mismatch; the actual requested editor was 9AD7D4AA and its normal UI completed.

Welcome to Civilization, Making Progress and Camera Tutorial were acknowledged
through their visible checkmarks. Holding both mouse buttons and moving the
camera advanced the rotation lesson normally. Holding VK_UP for two seconds
moved the camera but left the visible Use the arrow keys to pan the camera
instruction active, reproducing the earlier limitation. Both processes remained
alive. The Windows computer-use app-access approval timed out before native
Windows input could be tested. This still does not prove a player-facing defect;
no tutorial-skipping or gameplay patch was added.

The new --stop-at-arrival option on duo_tribe_graduation_cityhall.py is fixture
setup only: it stops at both native Civilization modes and does not claim
capital, design, tutorial or playable-HUD validation. Those checks must follow
the lesson. The initial run attempted capital validation while Welcome still
paused the world and failed; preserve lineages-tutorial-a-arrival-religious.log
as that test-driver failure, not proof of missing founder cities.

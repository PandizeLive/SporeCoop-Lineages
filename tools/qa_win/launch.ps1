# Start one muted QA Spore game from the QA copy (never the owner's own install).
#   tools/qa_win/launch.ps1 -Instance 9 -Profile DuoQAJiffHost [-Resolution 800x600]
# Owner's test settings (2026-11-12, docs/ROADMAP.md decision 11): small windows unless a test needs a size (Adventure
# clicks use 1440x1080); 5 FPS from the QA copy's mods/sporekit.ini (frame_ms=200). The QA copy needs steam_appid.txt
# (24720) next to SporeApp.exe so Steam does not relaunch the owner's install instead.
param([Parameter(Mandatory)][int]$Instance, [Parameter(Mandatory)][string]$Profile, [string]$Resolution = '800x600',
      [string]$Copy = $(if ($env:SPORECOOP_QA_COPY) { $env:SPORECOOP_QA_COPY } else { Join-Path $PSScriptRoot '..\..\out\release36\native' }))
$exe = Join-Path (Resolve-Path $Copy) 'SporebinEP1\SporeApp.exe'
if (-not (Test-Path (Join-Path (Split-Path $exe) 'steam_appid.txt'))) { throw "no steam_appid.txt next to $exe (Steam would relaunch the owner's install)" }
$env:SteamAppId = '24720'; $env:SteamGameId = '24720'
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru `
    -ArgumentList '-w', "-r:$Resolution", "-sporekit-profile:$Profile", "-sporekit-instance:$Instance"
"started instance $Instance pid $($p.Id) ($Resolution)"

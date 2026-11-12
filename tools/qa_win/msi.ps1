# The MSI test PC over Tailscale SSH (`ssh msi`, docs/06-testing.md#testing-two-pc-msi).
#   tools/qa_win/msi.ps1 deploy     strip and copy build/coop.dll + build/sporekit.dll + the GOG 2024 symbols to its Steam Spore
#   tools/qa_win/msi.ps1 start      start Spore there in the logged-on desktop (scheduled task, profile MSIGuest)
#   tools/qa_win/msi.ps1 stop       close Spore there
#   tools/qa_win/msi.ps1 tunnel     keep local TCP 47850 -> the MSI's console (127.0.0.1:47800); runs until closed
# The first deploy backs up the MSI's files to C:\temp\sporecoop-msi-backup-<date>.
param([Parameter(Mandatory)][ValidateSet('deploy', 'start', 'stop', 'tunnel')][string]$Action,
      [string]$Resolution = '1280x960', [string]$Profile = 'MSIGuest')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$game = 'C:\Program Files (x86)\Steam\steamapps\common\spore 24720\SporebinEP1'
$strip = Join-Path $env:LLVM_MINGW 'bin\llvm-strip.exe'
switch ($Action) {
    'deploy' {
        if (-not $env:LLVM_MINGW) { throw 'set LLVM_MINGW to the llvm-mingw folder (for llvm-strip)' }
        $tmp = Join-Path $env:TEMP 'sporecoop-msi'; New-Item -ItemType Directory -Force $tmp | Out-Null
        & $strip -o "$tmp\coop.dll" "$root\build\coop.dll"; & $strip -o "$tmp\sporekit.dll" "$root\build\sporekit.dll"
        Copy-Item "$root\data\symbols\overrides_dc04aee5.sym" $tmp -Force
        ssh -o BatchMode=yes msi 'New-Item -ItemType Directory -Force C:\temp\sporecoop-msi-in | Out-Null'
        scp -o BatchMode=yes "$tmp\coop.dll" "$tmp\sporekit.dll" "$tmp\overrides_dc04aee5.sym" msi:C:/temp/sporecoop-msi-in/
        $remote = @"
`$d = '$game'
if (Get-Process SporeApp -ErrorAction SilentlyContinue) { throw 'Spore is running on the MSI: stop it first' }
`$b = 'C:\temp\sporecoop-msi-backup-' + (Get-Date -Format yyyyMMdd)
if (-not (Test-Path "`$b\coop.dll")) { New-Item -ItemType Directory -Force `$b | Out-Null; Copy-Item "`$d\sporekit.dll","`$d\mods\coop.dll","`$d\mods\sporekit\symbols\overrides_dc04aee5.sym" `$b }
Copy-Item C:\temp\sporecoop-msi-in\sporekit.dll "`$d\sporekit.dll" -Force
Copy-Item C:\temp\sporecoop-msi-in\coop.dll "`$d\mods\coop.dll" -Force
Copy-Item C:\temp\sporecoop-msi-in\overrides_dc04aee5.sym "`$d\mods\sporekit\symbols\overrides_dc04aee5.sym" -Force
'deployed: ' + (Get-Item "`$d\mods\coop.dll").Length + ' bytes'
"@
        ssh -o BatchMode=yes msi $remote
    }
    'start' {
        # a game started from the SSH shell has no desktop: a scheduled task runs it in the logged-on session
        $cmd = "@echo off`r`nset SteamAppId=24720`r`nset SteamGameId=24720`r`ncd /d `"$game`"`r`nstart `"`" SporeApp.exe -w -r:$Resolution -sporekit-profile:$Profile -sporekit-instance:1`r`n"
        $tmp = Join-Path $env:TEMP 'spore-msi-test.cmd'; [IO.File]::WriteAllText($tmp, $cmd)
        scp -o BatchMode=yes $tmp msi:C:/temp/spore-msi-test.cmd
        ssh -o BatchMode=yes msi 'schtasks /create /tn SporeCoopMSITest /tr C:\temp\spore-msi-test.cmd /sc once /st 23:59 /it /f | Out-Null; schtasks /run /tn SporeCoopMSITest | Out-Null; Start-Sleep 15; Get-NetTCPConnection -State Listen -LocalPort 47800 -ErrorAction SilentlyContinue | ForEach-Object { "console up, pid " + $_.OwningProcess }'
    }
    'stop' { ssh -o BatchMode=yes msi 'Get-Process SporeApp -ErrorAction SilentlyContinue | Stop-Process -Force; "stopped"' }
    'tunnel' { ssh -o BatchMode=yes -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 -N -L 47850:127.0.0.1:47800 msi }
}

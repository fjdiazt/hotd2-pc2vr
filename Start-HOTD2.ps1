param(
    [string]$GameExe = 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe',
    [ValidateSet('VR','Window','Stereo')][string]$Mode = 'VR',
    [int]$Seconds = 0,
    [switch]$InfiniteAmmo,
    [switch]$InfiniteLives,
    [switch]$RapidFire,
    [ValidateRange(1,30)][int]$RapidFireRate = 3
)
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'build'
if (!(Test-Path "$out\HOTD2VR.exe") -or !(Test-Path "$out\HOTD2Bridge.dll")) { throw 'Run build.ps1 first.' }
$expected = 'C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A'
if ((Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash -ne $expected) { throw 'Unsupported HOTD2 executable; no process started.' }
& "$out\HOTD2Launch.exe" $GameExe
if ($LASTEXITCODE) { throw 'Game injection failed.' }
$viewerArgs = @()
if ($Mode -eq 'Window') { $viewerArgs += '--window' }
if ($Mode -eq 'Stereo') { $viewerArgs += '--stereo' }
if ($Seconds -gt 0) { $viewerArgs += '--seconds', "$Seconds" }
if ($InfiniteAmmo) { $viewerArgs += '--infinite-ammo' }
if ($InfiniteLives) { $viewerArgs += '--infinite-lives' }
if ($RapidFire) { $viewerArgs += '--rapid-fire' }
if ($PSBoundParameters.ContainsKey('RapidFireRate')) { $viewerArgs += '--rapid-fire-rate', "$RapidFireRate" }
& "$out\HOTD2VR.exe" @viewerArgs
exit $LASTEXITCODE

param(
    [string]$GameExe = 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe',
    [ValidateSet('VR','Window','Stereo')][string]$Mode = 'VR',
    [int]$Seconds = 0,
    [switch]$InfiniteAmmo,
    [switch]$InfiniteLives,
    [switch]$Invincibility,
    [switch]$InfiniteContinues,
    [switch]$RapidFire,
    [ValidateRange(1,30)][int]$RapidFireRate = 4,
    [switch]$SkipSplash,
    [switch]$ShowSplash,
    [ValidateRange(0.1,10.0)][double]$WorldScale = 1.25,
    [ValidateRange(25,400)][int]$Brightness = 85,
    [switch]$NativeLighting,
    [switch]$NoNativeLighting,
    [switch]$Laser,
    [switch]$NoLaser,
    [switch]$Crosshair,
    [switch]$NoCrosshair
)
$ErrorActionPreference = 'Stop'
if ($SkipSplash -and $ShowSplash) { throw 'Choose either -SkipSplash or -ShowSplash.' }
if ($NativeLighting -and $NoNativeLighting) { throw 'Choose either -NativeLighting or -NoNativeLighting.' }
if ($Laser -and $NoLaser) { throw 'Choose either -Laser or -NoLaser.' }
if ($Crosshair -and $NoCrosshair) { throw 'Choose either -Crosshair or -NoCrosshair.' }
$out = Join-Path $PSScriptRoot 'build'
if (!(Test-Path "$out\HOTD2VR.exe") -or !(Test-Path "$out\HOTD2Bridge.dll")) { throw 'Run build.ps1 first.' }
$expected = 'C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A'
if ((Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash -ne $expected) { throw 'Unsupported HOTD2 executable; no process started.' }
$launchArgs = @($GameExe)
if ($SkipSplash) { $launchArgs += '--skip-splash' }
if ($ShowSplash) { $launchArgs += '--show-splash' }
& "$out\HOTD2Launch.exe" @launchArgs
if ($LASTEXITCODE) { throw 'Game injection failed.' }
$viewerArgs = @()
if ($Mode -eq 'Window') { $viewerArgs += '--window' }
if ($Mode -eq 'Stereo') { $viewerArgs += '--stereo' }
if ($Seconds -gt 0) { $viewerArgs += '--seconds', "$Seconds" }
if ($InfiniteAmmo) { $viewerArgs += '--infinite-ammo' }
if ($InfiniteLives) { $viewerArgs += '--infinite-lives' }
if ($Invincibility) { $viewerArgs += '--invincibility' }
if ($InfiniteContinues) { $viewerArgs += '--infinite-continues' }
if ($RapidFire) { $viewerArgs += '--rapid-fire' }
if ($PSBoundParameters.ContainsKey('RapidFireRate')) { $viewerArgs += '--rapid-fire-rate', "$RapidFireRate" }
if ($PSBoundParameters.ContainsKey('WorldScale')) { $viewerArgs += '--world-scale', $WorldScale.ToString('G', [Globalization.CultureInfo]::InvariantCulture) }
if ($PSBoundParameters.ContainsKey('Brightness')) { $viewerArgs += '--brightness', "$Brightness" }
if ($NativeLighting) { $viewerArgs += '--native-lighting' }
if ($NoNativeLighting) { $viewerArgs += '--no-native-lighting' }
if ($Laser) { $viewerArgs += '--laser' }
if ($NoLaser) { $viewerArgs += '--no-laser' }
if ($Crosshair) { $viewerArgs += '--crosshair' }
if ($NoCrosshair) { $viewerArgs += '--no-crosshair' }
& "$out\HOTD2VR.exe" @viewerArgs
exit $LASTEXITCODE

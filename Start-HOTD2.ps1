param(
    [string]$GameExe = 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe',
    [ValidateSet('VR','Window','Stereo')][string]$Mode = 'VR',
    [switch]$Window,
    [switch]$NoWindow,
    [switch]$VrWindow,
    [switch]$NoVrWindow,
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
    [ValidateRange(25,400)][int]$Brightness = 90,
    [ValidateRange(0,100)][int]$MasterVolume = 100,
    [ValidateRange(0,100)][int]$MusicVolume = 100,
    [ValidateRange(0,100)][int]$EffectsVolume = 100,
    [switch]$ExpandMeshes,
    [switch]$NoExpandMeshes,
    [switch]$ExpandWorld,
    [switch]$NoExpandWorld,
    [switch]$NativeLighting,
    [switch]$NoNativeLighting,
    [switch]$Laser,
    [switch]$NoLaser,
    [switch]$Crosshair,
    [switch]$NoCrosshair,
    [switch]$SwapPlayerHud,
    [switch]$NoSwapPlayerHud,
    [int]$StartStage,
    [int]$StartScene,
    [switch]$ContinueGame,
    [switch]$NoContinueGame,
    [switch]$SaveCheckpoint
)
$ErrorActionPreference = 'Stop'
if ($Window -and $NoWindow) { throw 'Choose either -Window or -NoWindow.' }
if ($VrWindow -and $NoVrWindow) { throw 'Choose either -VrWindow or -NoVrWindow.' }
if ($SkipSplash -and $ShowSplash) { throw 'Choose either -SkipSplash or -ShowSplash.' }
if ($ExpandMeshes -and $NoExpandMeshes) { throw 'Choose either -ExpandMeshes or -NoExpandMeshes.' }
if ($ExpandWorld -and $NoExpandWorld) { throw 'Choose either -ExpandWorld or -NoExpandWorld.' }
if ($NativeLighting -and $NoNativeLighting) { throw 'Choose either -NativeLighting or -NoNativeLighting.' }
if ($Laser -and $NoLaser) { throw 'Choose either -Laser or -NoLaser.' }
if ($Crosshair -and $NoCrosshair) { throw 'Choose either -Crosshair or -NoCrosshair.' }
if ($SwapPlayerHud -and $NoSwapPlayerHud) { throw 'Choose either -SwapPlayerHud or -NoSwapPlayerHud.' }
if ($ContinueGame -and $NoContinueGame) { throw 'Choose either -ContinueGame or -NoContinueGame.' }
if ($PSBoundParameters.ContainsKey('StartStage') -and ($StartStage -lt 1 -or $StartStage -gt 6)) { throw "Invalid stage $StartStage" }
if ($PSBoundParameters.ContainsKey('StartScene') -and ($StartScene -lt 0 -or $StartScene -gt 255)) { throw "Invalid scene $StartScene" }
if ($PSBoundParameters.ContainsKey('StartScene') -and !$PSBoundParameters.ContainsKey('StartStage')) { throw '-StartScene requires -StartStage.' }
$out = Join-Path $PSScriptRoot 'build'
if ($SaveCheckpoint) {
    if ($PSBoundParameters.Count -ne 1) { throw '-SaveCheckpoint must be used alone.' }
    if (!(Test-Path "$out\HOTD2VR.exe")) { throw 'Run build.ps1 first.' }
    & "$out\HOTD2VR.exe" --save-checkpoint
    exit $LASTEXITCODE
}
if (!(Test-Path "$out\HOTD2VR.exe") -or !(Test-Path "$out\HOTD2Bridge.dll")) { throw 'Run build.ps1 first.' }
$expected = 'C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A'
if ((Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash -ne $expected) { throw 'Unsupported HOTD2 executable; no process started.' }
$launchArgs = @($GameExe)
if ($SkipSplash) { $launchArgs += '--skip-splash' }
if ($ShowSplash) { $launchArgs += '--show-splash' }
if ($PSBoundParameters.ContainsKey('StartStage')) { $launchArgs += '--start-stage', "$StartStage" }
if ($PSBoundParameters.ContainsKey('StartScene')) { $launchArgs += '--start-scene', "$StartScene" }
if ($ContinueGame) { $launchArgs += '--continue-game' }
if ($NoContinueGame) { $launchArgs += '--no-continue-game' }
& "$out\HOTD2Launch.exe" @launchArgs
if ($LASTEXITCODE) { throw 'Game injection failed.' }
$viewerArgs = @()
if ($PSBoundParameters.ContainsKey('Mode') -and $Mode -eq 'VR') { $viewerArgs += '--vrwindow' }
if ($Mode -eq 'Window') { $viewerArgs += '--window', '--no-vrwindow' }
if ($Mode -eq 'Stereo') { $viewerArgs += '--stereo' }
if ($Window) { $viewerArgs += '--window' }
if ($NoWindow) { $viewerArgs += '--no-window' }
if ($VrWindow) { $viewerArgs += '--vrwindow' }
if ($NoVrWindow) { $viewerArgs += '--no-vrwindow' }
if ($Seconds -gt 0) { $viewerArgs += '--seconds', "$Seconds" }
if ($InfiniteAmmo) { $viewerArgs += '--infinite-ammo' }
if ($InfiniteLives) { $viewerArgs += '--infinite-lives' }
if ($Invincibility) { $viewerArgs += '--invincibility' }
if ($InfiniteContinues) { $viewerArgs += '--infinite-continues' }
if ($RapidFire) { $viewerArgs += '--rapid-fire' }
if ($PSBoundParameters.ContainsKey('RapidFireRate')) { $viewerArgs += '--rapid-fire-rate', "$RapidFireRate" }
if ($PSBoundParameters.ContainsKey('WorldScale')) { $viewerArgs += '--world-scale', $WorldScale.ToString('G', [Globalization.CultureInfo]::InvariantCulture) }
if ($PSBoundParameters.ContainsKey('Brightness')) { $viewerArgs += '--brightness', "$Brightness" }
if ($PSBoundParameters.ContainsKey('MasterVolume')) { $viewerArgs += '--master-volume', "$MasterVolume" }
if ($PSBoundParameters.ContainsKey('MusicVolume')) { $viewerArgs += '--music-volume', "$MusicVolume" }
if ($PSBoundParameters.ContainsKey('EffectsVolume')) { $viewerArgs += '--effects-volume', "$EffectsVolume" }
if ($ExpandMeshes) { $viewerArgs += '--expand-meshes' }
if ($NoExpandMeshes) { $viewerArgs += '--no-expand-meshes' }
if ($ExpandWorld) { $viewerArgs += '--expand-world' }
if ($NoExpandWorld) { $viewerArgs += '--no-expand-world' }
if ($NativeLighting) { $viewerArgs += '--native-lighting' }
if ($NoNativeLighting) { $viewerArgs += '--no-native-lighting' }
if ($Laser) { $viewerArgs += '--laser' }
if ($NoLaser) { $viewerArgs += '--no-laser' }
if ($Crosshair) { $viewerArgs += '--crosshair' }
if ($NoCrosshair) { $viewerArgs += '--no-crosshair' }
if ($SwapPlayerHud) { $viewerArgs += '--swap-player-hud' }
if ($NoSwapPlayerHud) { $viewerArgs += '--no-swap-player-hud' }
& "$out\HOTD2VR.exe" @viewerArgs
exit $LASTEXITCODE

# HOTD2 PC2VR

Experimental native VR conversion of the original Windows **The House of the Dead 2**. Source lives here; the game remains in its existing installation. No game assets or executable patches are distributed.

The x86 launcher starts the game suspended, injects `HOTD2Bridge.dll`, and resumes it. The bridge captures the game's polygon submissions, textures and live Direct3D7 projection, then publishes them to an x64 OpenXR viewer adapted from NeuralF's VC2VR. The VC2VR revolver mesh, texture, recoil and controller rendering are reused.

## Build and launch

Requirements: Windows, 32-bit MinGW GCC, Visual Studio C++ x64 tools. OpenXR headers are included. The build copies the installed VC2VR OpenXR loader when available; otherwise provide the official x64 `openxr_loader.dll` beside the viewer.

```powershell
cd C:\gaming\hotd2-pc2vr
.\build.ps1
.\Start-HOTD2.ps1 -Mode Window
.\Start-HOTD2.ps1 -Mode Stereo
.\Start-HOTD2.ps1 -Mode VR
```

Close the existing game before starting a new session. Injected sessions accept the native quit confirmation automatically in desktop and VR modes. This bypasses the question whenever the game asks it; Back retains its existing behavior rather than becoming a new quit shortcut. `Start-HOTD2.ps1` defaults to `N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe`; `-GameExe` overrides it. `-Seconds 20` bounds a viewer test; the game continues running afterward. To reconnect a viewer to the running bridge, run `build\HOTD2VR.exe` directly (`--window` or `--stereo` for desktop).

`build\HOTD2VR.ini` is the active configuration; subsequent builds preserve it. `HOTD2VR.ini` in the source root supplies defaults for a new build folder.

### Skip startup splashes

Startup skips the SEGA and Empire logos by default, including when `[bridge] skip_splash` is absent from `build\HOTD2VR.ini`. Set `skip_splash=0` to restore both logos.

`Start-HOTD2.ps1 -SkipSplash` and `-ShowSplash` override the INI for one launch. Direct launcher equivalents are `build\HOTD2Launch.exe <game.exe> --skip-splash` and `--show-splash`; they also work with `--wait` or `--native-window`. The last splash flag wins. Viewer-only reconnects cannot change startup behavior.

The launcher validates the supported startup instruction and changes one byte in suspended process memory before the game starts. The game executable on disk and its `Hod2.ini` remain unchanged. Changes take effect on the next launch.

## Windowed startup

The verified executable hardcodes mode 11 (640x480, 16-bit fullscreen). The bridge selects native mode 7 (640x480, 32-bit windowed) in memory. It automatically accepts the native renderer-selection question ending in `will be used.` and the exact native `Are you sure you want to quit?` question. Other dialogs retain their normal behavior. Background rendering prevents the native activation handler from suspending rendering when the viewer takes focus.

No external wrapper, game INI change or on-disk executable edit is required. Launching `Hod2.exe` normally retains its original behavior. `[bridge] windowed=0` or `background=0` disables the respective change for the next injected launch.

Injected sessions also restore opaque alpha after the game's two GDI bitmap-upload paths. This fixes the black/clipped game-mode menu on the tested renderer while preserving native alpha testing and RGB artwork. The mode-selection screen is verified on desktop and user-confirmed in VR; other screens still need visual checks.

The bridge also relaxes native per-mesh culling at the flat screen's left/right/top/bottom edges, so offscreen body parts can reach the VR renderer. Near/far and custom clipping planes remain. Native per-draw backface culling is preserved in the viewer. Desktop capture confirms additional offscreen geometry. User reported the headset result looks good after the backface correction; broader scene coverage and performance still need testing.

For a native desktop comparison, run `build\HOTD2Launch.exe 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe' --native-window`. This selects only the native windowed mode in memory, without injecting the bridge or starting a viewer. Native renderer and quit questions remain.

## World scale

World size defaults to **1.4×**. Set `[vr] worldscale=1.4` in `build\HOTD2VR.ini`, use `Start-HOTD2.ps1 -WorldScale 1.4`, or pass `--world-scale 1.4` directly to `build\HOTD2VR.exe`. Accepted multipliers range from 0.1 to 10; **1 restores the previous world size**, 0.8 makes it 20% smaller, and 2 doubles it. Command-line values override the INI for that viewer session. Convert older percentage settings by dividing by 100 (`140` becomes `1.4`).

World geometry and controller ray conversion use the same scale. The HUD, floating menus, and controller gun models retain their physical size. `[bridge] units_per_metre` remains the base game-unit conversion. Restart the viewer after editing the INI; the game does not need a restart. Headset scale and aiming still need visual confirmation.

## Controls

Gun laser and VR crosshair default **on**. Each setting applies to both hands. Set `[vr] laser=0` to hide the beam and muzzle flare, or `crosshair=0` to hide the aiming ring, core and ticks in `build\HOTD2VR.ini`. Use `1` to enable either; missing keys default to `1`. These settings affect the viewer's aim guides; gun models, aiming and firing keep their normal behavior.

For one launch, use `Start-HOTD2.ps1 -NoLaser -NoCrosshair`, or `build\HOTD2VR.exe --no-laser --no-crosshair` when reconnecting a viewer. `-Laser` / `-Crosshair` and `--laser` / `--crosshair` override disabled INI settings. Each option works separately; direct CLI flags use the last value supplied. Restart the viewer after changing the INI.

VR startup centers only after OpenXR reports a focused session and valid, actively tracked stereo head poses. Earlier bootstrap poses are ignored. Later focus changes preserve the center; grip requests a new center. User confirmed startup centering works in the headset.

VR: trigger fires. Aim the gun at least 50 degrees above or 45 degrees below horizontal and pull the trigger to reload that gun. Direction comes from the tracked gun, independent of head direction. Each pull stays a shot or reload until release; tracking loss cancels it until release. Both hands work independently in akimbo.

World aim uses captured model geometry, including close targets and coordinates outside the original 640×480 viewport. Hits more than 1 mm ahead of the controller ray are accepted; native signed 16-bit axis limits remain. HUD sprites and shot flashes are excluded from aim intersection. Floating-menu aiming stays within the panel. Native shot flashes are placed just in front of the surface under their original camera ray, preserving their angular size; misses use `aimreach`. User confirmed close-enemy aiming and splash placement work well in the headset.

A/X keep native Start/confirm/pause behavior; B/Y send native Reload/Cancel for menu cancellation and cutscene skip; grip recenters. During play, B/Y also reload the corresponding gun in either mode (B right/P1, Y left/P2). Holding Cancel does not block a later trigger pull. Trigger-angle reload remains available and sends no pause keys. Both controller guns use the supplied VC2VR mesh. Gesture comfort, cancel/skip behavior and player 2's full join/reload flow still require headset testing.

`[vr] akimbo` uses VC2VR's setting name. Right always controls P1; left always controls P2. With `akimbo=1`, both tracked guns appear from startup, and either hand's Start sends both native player Start keys. A single menu confirmation joins both players. With `akimbo=0`, gun visibility follows actual native player status. Start requests a join for that hand's player; pressing it in a menu or during a rejected join does not reveal a gun. Accepted joins waiting through a cutscene remain visible; continue countdowns, game over and returning to menus hide inactive guns. Both guns appear when both players are active; neither hand takes over the other player's gun. Firing or lost tracking cannot switch player identities, and a hidden hand's held trigger must be released before it can fire after joining. Grip and menu navigation remain available before joining. Restart the viewer after changing the INI.

Paired joining and both independent join orders passed desktop native-game checks. Controller visibility/player routing passed mocked OpenXR checks; the corrected independent-player headset presentation still needs confirmation.

Either thumbstick navigates the game-mode menu up/down through native arrow keys. Center releases the key; holding uses native repeat. Opposing sticks cancel VR navigation. Navigation is limited to native phase 4, so it cannot move keyboard aim during gameplay. Thumbstick zoom and click-to-toggle-screen bindings were removed; the floating menu screen still appears automatically. A confirms and B goes back.

Desktop preview: W/A/S/D move, mouse drag turns, R resets, M toggles the captured game panel, Escape closes the viewer. Stereo preview provides a desktop check without a headset.

## Optional cheats

All default off. Enable per launch through the launcher:

```powershell
.\Start-HOTD2.ps1 -Mode VR -InfiniteLives -InfiniteAmmo -RapidFire -RapidFireRate 4
```

For a viewer attached to an already injected game, use `build\HOTD2VR.exe --infinite-lives --infinite-ammo --rapid-fire --rapid-fire-rate 4`. These flag names match VC2VR for lives/ammo. Omit unwanted flags. Flags enable cheats for that viewer session without changing the INI. The rate option overrides the INI for that launch; it does not enable rapid fire by itself.

Persistent settings live in `build\HOTD2VR.ini`:

```ini
[vr]
infinite_lives=1
infinite_ammo=1
rapid_fire=1
rapid_fire_rate=4
```

Lives/ammo use the game's native cheat flags for both players, including outside akimbo. They also work with desktop preview. Existing game settings are restored when the viewer closes or its cheat heartbeat expires after 500 ms. The original `Hod2.ini` is untouched. If ammo is already empty when enabling the cheat, reload once.

Rapid fire repeats a held VR trigger at up to `rapid_fire_rate` shots/sec, independently per hand in akimbo. Default is **4 shots/sec**; allowed values are whole numbers 1–30. Native frame timing and weapon handling can lower the actual rate. Reload gestures still take priority; menus, paused play, cutscenes, inactive players and stale input receive no repeated shots. VR gun recoil/haptics retain their existing trigger-press behavior.

Desktop testing at the earlier 10 shots/sec setting observed 11 shots with ammo held at 6, then ammo dropping to 0 after disabling infinite ammo. Native flag activation/restoration and desktop command-line delivery passed. Damage protection, P2 gameplay and Original-mode weapons still need runtime confirmation.

## Validation

`build.ps1` runs projection/FVF/prompt, native trigger-edge, cheat configuration/restoration, rapid-fire timing/guards, atlas allocation, cache eviction, frame-snapshot and offscreen D3D11 effect-pixel checks. These checks start neither the game nor OpenXR. With attract gameplay or a game running through the injected bridge:

```powershell
python .\tests\test_live.py
python .\tests\test_live.py --input
```

Close the viewer before the second command. It briefly drives both aim coordinates without firing, then verifies the input watchdog expires and the native axis-write instructions remain intact. Logs and diagnostic images stay under ignored `build\`.

## Current boundaries

- Native geometry, texture capture, gun asset loading and desktop rendering have been exercised. See `docs/findings.md` for evidence and remaining checks.
- User confirmed the 3D scene, both guns, good aiming, working trigger firing and immersive reload in the headset. Revised 50-degree up/45-degree down thresholds, head tracking, stereoscopic comfort, real-world scale and two-player joining need further headset checks. The initial `units_per_metre=10` is uncalibrated.
- Only geometry submitted by the original game is available. Per-mesh screen-edge rejection is relaxed, but whole-object/stage culling and intentionally absent surfaces can still leave gaps when turning or leaning.
- White artifacts persisted after atlas repair. Native material color/opacity and blend states were being discarded; those now pass capture and actual shader pixel checks. User confirmed the result looks better in the headset. Full native lighting, transparency ordering, texture addressing and HUD depth still need visual qualification. A single scene must fit the atlas; mutable textures need dirty tracking if encountered.
- The supported game has SHA256 `C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A` (MD5 `97C9E516A287AAB33A455A396DADAA45`). Native hooks validate instruction signatures before writing.

## Rollback

Close the viewer and game. All hooks and native mode changes disappear with the game process. The mod does not deploy DLLs into the game directory. A copy of the original `Hod2.ini` from the first desktop test is retained at `build\Hod2.ini.before-test`; the mod itself does not edit it.

Credits and license: `THIRD-PARTY.md`, `LICENSE`.

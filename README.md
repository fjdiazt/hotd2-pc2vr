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

## Windowed startup

The verified executable hardcodes mode 11 (640x480, 16-bit fullscreen). The bridge selects native mode 7 (640x480, 32-bit windowed) in memory. It automatically accepts the native renderer-selection question ending in `will be used.` and the exact native `Are you sure you want to quit?` question. Other dialogs retain their normal behavior. Background rendering prevents the native activation handler from suspending rendering when the viewer takes focus.

No external wrapper, game INI change or on-disk executable edit is required. Launching `Hod2.exe` normally retains its original behavior. `[bridge] windowed=0` or `background=0` disables the respective change for the next injected launch.

Injected sessions also restore opaque alpha after the game's two GDI bitmap-upload paths. This fixes the black/clipped game-mode menu on the tested renderer while preserving native alpha testing and RGB artwork. The mode-selection screen is verified on desktop and user-confirmed in VR; other screens still need visual checks.

The bridge also relaxes native per-mesh culling at the flat screen's left/right/top/bottom edges, so offscreen body parts can reach the VR renderer. Near/far and custom clipping planes remain. Native per-draw backface culling is preserved in the viewer. Desktop capture confirms additional offscreen geometry. User reported the headset result looks good after the backface correction; broader scene coverage and performance still need testing.

For a native desktop comparison, run `build\HOTD2Launch.exe 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe' --native-window`. This selects only the native windowed mode in memory, without injecting the bridge or starting a viewer. Native renderer and quit questions remain.

## Controls

VR startup centers only after OpenXR reports a focused session and valid, actively tracked stereo head poses. Earlier bootstrap poses are ignored. Later focus changes preserve the center; grip requests a new center. User confirmed startup centering works in the headset.

VR: trigger fires. Aim the gun at least 50 degrees above or 45 degrees below horizontal and pull the trigger to reload that gun. Direction comes from the tracked gun, independent of head direction. Each pull stays a shot or reload until release; tracking loss cancels it until release. Both hands work independently in akimbo.

A/X keep native Start/confirm/pause behavior; B/Y send native Reload/Cancel for menu cancellation and cutscene skip; grip recenters. During play, B/Y also reload the corresponding gun, independently in akimbo (B right/P1, Y left/P2). Holding Cancel does not block a later trigger pull. Trigger-angle reload remains available and sends no pause keys. Both controller guns use the supplied VC2VR mesh. Gesture comfort, cancel/skip behavior and player 2's full join/reload flow still require headset testing.

Either thumbstick navigates the game-mode menu up/down through native arrow keys. Center releases the key; holding uses native repeat. Opposing sticks cancel VR navigation. Navigation is limited to native phase 4, so it cannot move keyboard aim during gameplay. Thumbstick zoom and click-to-toggle-screen bindings were removed; the floating menu screen still appears automatically. A confirms and B goes back.

Desktop preview: W/A/S/D move, mouse drag turns, R resets, M toggles the captured game panel, Escape closes the viewer. Stereo preview provides a desktop check without a headset.

## Validation

`build.ps1` runs projection/FVF/prompt, native trigger-edge, atlas allocation, cache eviction, frame-snapshot and offscreen D3D11 effect-pixel checks. These checks start neither the game nor OpenXR. With attract gameplay or a game running through the injected bridge:

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

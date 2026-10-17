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

Close the existing game before starting a new session. The native quit confirmation can block closing: end only the intended `Hod2.exe` test process if necessary. `Start-HOTD2.ps1` defaults to `N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe`; `-GameExe` overrides it. `-Seconds 20` bounds a viewer test; the game continues running afterward. To reconnect a viewer to the running bridge, run `build\HOTD2VR.exe` directly (`--window` or `--stereo` for desktop).

`build\HOTD2VR.ini` is the active configuration; subsequent builds preserve it. `HOTD2VR.ini` in the source root supplies defaults for a new build folder.

## Windowed startup

The verified executable hardcodes mode 11 (640x480, 16-bit fullscreen). The bridge selects native mode 7 (640x480, 32-bit windowed) in memory. It automatically accepts only the native renderer-selection question ending in `will be used.`; other error/confirmation dialogs remain intact. Background rendering prevents the native activation handler from suspending rendering when the viewer takes focus.

No external wrapper, game INI change or on-disk executable edit is required. Launching `Hod2.exe` normally retains its original behavior. `[bridge] windowed=0` or `background=0` disables the respective change for the next injected launch.

## Controls

VR: trigger fires; A/X reloads and confirms/starts; B/Y backs out; grip recenters. Both controller guns use the supplied VC2VR mesh. Player 2's full join/control flow still requires headset testing.

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
- User confirmed the 3D scene, both guns, good aiming and working trigger firing in the headset. Head tracking, stereoscopic comfort, real-world scale, reloading and two-player joining need further headset checks. The initial `units_per_metre=10` is uncalibrated.
- Only geometry submitted by the original game is available. Turning or leaning may expose culled or missing surfaces.
- White artifacts persisted after atlas repair. Native material color/opacity and blend states were being discarded; those now pass capture and actual shader pixel checks. User confirmed the result looks better in the headset. Full native lighting, transparency ordering, texture addressing and HUD depth still need visual qualification. A single scene must fit the atlas; mutable textures need dirty tracking if encountered.
- The supported game has SHA256 `C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A` (MD5 `97C9E516A287AAB33A455A396DADAA45`). Native hooks validate instruction signatures before writing.

## Rollback

Close the viewer and game. All hooks and native mode changes disappear with the game process. The mod does not deploy DLLs into the game directory. A copy of the original `Hod2.ini` from the first desktop test is retained at `build\Hod2.ini.before-test`; the mod itself does not edit it.

Credits and license: `THIRD-PARTY.md`, `LICENSE`.

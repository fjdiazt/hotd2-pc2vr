# HOTD2 PC2VR

An experimental PC VR mod for the original Windows version of **The House of the Dead 2**. The mod captures the running game's geometry and textures, reconstructs the scene around you, and renders it through OpenXR. Your motion controllers become the light guns.

The viewer is adapted from [NeuralF's Virtua Cop 2 VR (VC2VR)](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR). Its OpenXR, controller, sight and gun-rendering code provided the foundation; the launcher, Direct3D7 capture and native input integration are specific to HOTD2. The revolver model and texture also come from VC2VR; see [Credits](#credits).

- Stereo VR with head tracking and controller aiming against captured world geometry.
- Right controller as P1, left controller as P2; paired akimbo or independent joining.
- Physical reload gesture, recoil and haptics.
- Floating menus and HUD, with separate laser and crosshair switches.
- G's File boss briefings at approximately one metre, preserving their animated 3D pages and native depth testing.
- Configurable world size, picture settings and optional cheats.
- Startup-logo bypass and native windowed startup through memory-only changes.

Your existing game installation stays in place. No HOTD2 game files are included or changed on disk.

## Requirements

- Your own working Windows PC copy of The House of the Dead 2.
- Windows x64, a Direct3D 11-capable GPU, and a PC VR headset with an active OpenXR runtime.
- PowerShell, 32-bit MinGW GCC (`i686-w64-mingw32`), and Visual Studio C++ x64 build tools with a Windows SDK.
- The x64 `openxr_loader.dll` beside the built viewer. OpenXR headers are included in this repository.

Only this `Hod2.exe` build is currently supported:

```text
SHA256 C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A
MD5    97C9E516A287AAB33A455A396DADAA45
```

The launcher checks the SHA256 before starting a game; native hooks also check their instruction signatures. Other executable layouts, the Remake, and emulated versions are outside the supported target.

## Build and run

From the repository root:

```powershell
.\build.ps1
.\Start-HOTD2.ps1 -GameExe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' -Mode VR
```

Replace the example game path with your own. Supplying `-GameExe` avoids the launch script's machine-specific development default.

The build produces `HOTD2Launch.exe`, `HOTD2Bridge.dll`, `HOTD2VR.exe`, the gun assets and `HOTD2VR.ini` under `build\`. It runs offline checks before reporting success. It copies an OpenXR loader from the configured local VC2VR installation when available; otherwise place the official x64 loader in `build\` yourself.

1. Close any existing `Hod2.exe` session.
2. Start your OpenXR runtime and connect the headset.
3. Run `Start-HOTD2.ps1` with your game path.
4. Face forward while the headset becomes focused and tracked. Right thumbstick click recenters afterward.

Keep the bridge and viewer from the same build together: their shared-memory formats must match. No DLL needs to be copied into the game directory.

### Desktop previews and reconnecting

```powershell
.\Start-HOTD2.ps1 -GameExe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' -Mode Window
.\Start-HOTD2.ps1 -GameExe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' -Mode Stereo
```

`Window` shows the reconstructed scene on the desktop; `Stereo` shows both eyes side by side. These help distinguish capture/rendering problems from OpenXR problems.

To attach a new viewer to an already injected game, run `build\HOTD2VR.exe` directly, optionally with `--window --no-vrwindow` or `--stereo`. Closing the viewer leaves the game running; closing the connected game ends the viewer. `-Seconds 20` limits the viewer session without closing the game.

Desktop preview controls: W/A/S/D move, mouse drag turns, R resets, M cycles the floating panel through auto/on/off, and Escape closes the viewer.

Under `[vr]`, `window` controls the existing reconstructed desktop preview and `vrwindow` controls headset rendering independently. The native game window stays available in every combination; `[bridge] windowed` only selects its windowed/fullscreen mode.

| `window` | `vrwindow` | Outputs besides native game window |
| --- | --- | --- |
| `0` | `0` | None; game controls/cheats remain active |
| `1` | `0` | Desktop preview |
| `0` | `1` | Headset (default) |
| `1` | `1` | Desktop preview and headset |

CLI overrides: `--window` / `--no-window`, `--vrwindow` / `--no-vrwindow`. For both outputs, use `HOTD2VR.exe --window --vrwindow` or `Start-HOTD2.ps1 -Window -VrWindow`. `-Mode Window` selects preview only; `-Mode VR` enables the headset while preserving the INI preview setting. With no explicit mode/switches, the INI selects both outputs. `--stereo` remains a separate desktop diagnostic mode.

With both outputs enabled, the preview uses the existing independent desktop camera and runs read-only; only the VR viewer sends controller input, cheats and audio controls. Closing the preview leaves the headset and native game running. Closing the VR viewer closes its preview; closing the native game ends both. This is not a left-eye mirror. Running both renderers costs additional CPU/GPU work.

## Controls

| Input | Action |
| --- | --- |
| Trigger | Native Start/confirm and fire, or reload while the gun is tilted beyond the reload threshold |
| Gun relative to headset: up 50°, down 45°, or sides 60°, then trigger | Reload that hand's gun |
| A / X | Native Start/confirm and pause/resume |
| B / Y | Native Skip/Cancel, without reload |
| Grip | Reload that hand's gun, without skip/cancel |
| Thumbstick four-way | Navigate native menus and adjust options |
| Right thumbstick click | Recenter the VR view |
| Left thumbstick click | Unmapped; reserved |

Trigger supplies Start and Fire; A/X supplies Start and Pause. The native game consumes them in its own interfaces. The pause input reader ignores synthetic trigger Start and accepts A/X press edges, retaining native eligibility, player ownership and automatic device-disconnect pause. Physical Enter/Tab retain native Start/pause. No menu classifier is used. Matching protocol-18 bridge/viewer builds are required.

Reload direction is measured from each tracked gun relative to the current headset orientation: up **50°**, down **45°**, or left/right **60°**, then pull the trigger. Head pitch/yaw/roll defines the reference; scripted game-camera motion and recenter offsets do not change these thresholds. A trigger pull remains a shot or reload until release; lost tracking disarms it until release. Grip supplies Reload only; B/Y supplies Skip/Cancel only. Two signature-checked weapon input reads preserve native reload rules while separating those commands. Physical Ctrl retains native behavior. Both sticks navigate native menus; right click recenters once per press. Left click is unbound and reserved. Index uses A/B on each hand; Vive uses menu for Start/Pause, grip for Reload and right trackpad click for recenter.

### Player HUD placement

`[vr] swap_player_hud=1` (default) moves converted gameplay ammo and lives rows to match the fixed gun assignments: P1 on the right, P2 on the left. Player labels, Original-mode weapon counts and reload messages/hints move with their row. Text, digit order, artwork, animation and HUD depth stay intact. Each row is aligned to the opposite side using its complete bounds; its width can vary with ammo/lives or messages.

Set `swap_player_hud=0`, use viewer `--no-swap-player-hud`, or launcher `-NoSwapPlayerHud` to retain the native layout. `--swap-player-hud` / `-SwapPlayerHud` enable it. Last direct CLI switch wins; the launcher rejects opposing switches. The flag works independently of `akimbo` and never changes controller/player assignments.

Ownership comes from the native player ammo/lives draw routines, including deferred sprite copies. Shared artwork elsewhere, captions, menus, world geometry and briefing-book geometry are retained. This change affects converted HUD geometry in the headset/desktop preview. The native game window and native framebuffer panels keep their original pixels and layout. Matching protocol 18 bridge/viewer builds are required. Akimbo headset placement is user-confirmed; non-akimbo placement still needs runtime confirmation.

### Experimental scene checkpoints

To start a new Arcade game at another chapter, use `./Start-HOTD2.ps1 -StartStage 6`. `-StartStage` accepts 1–6. Optional `-StartScene 3` selects the native zero-based scene ID within that stage; omitted scene uses the chapter entrance (scene 7 for chapter 3, scene 0 for the others). These options change the next **new Arcade start after the normal menus**, rather than jumping immediately from the title. Original, Training, Boss and attract modes keep their native behavior.

Enable `[vr] continue_game=1` to record the latest Arcade stage/scene and resume it on subsequent new Arcade starts. Default is `0`. Launcher switches `-ContinueGame` / `-NoContinueGame` override this for one launch; opposing PowerShell switches are rejected. Explicit `-StartStage` / `-StartScene` take priority over the saved location. With recording enabled, subsequent scene changes update the checkpoint even after an explicit jump.

The bridge stores `[checkpoint] version=1`, `stage=1..6` and `scene=0..255` in `HOTD2VR.checkpoint.ini` beside its DLL, separate from both configuration and native game saves. It writes only when the location changes during active Arcade gameplay, using a flushed temporary file and atomic replacement. No player health, score, inventory, enemy state, route history or exact script position is saved. Resume restarts the selected scene. Disabling the feature retains the file; deleting it starts normally until another checkpoint is recorded.

Direct launcher equivalents are `HOTD2Launch.exe path-to-Hod2.exe --start-stage 6 --start-scene 3 --continue-game`; `--no-continue-game` disables recording/resume. These startup options belong to `HOTD2Launch.exe`, not the viewer; `--native-window` cannot be combined with an explicit checkpoint request because it skips bridge injection. Stage/scene selection is CLI-only; only the recording/resume toggle is in the INI. A settings-menu control is not implemented here.

Invalid numeric arguments report `Invalid stage 20` or `Invalid scene 256` before starting a process. A missing direct-launcher value reports `Invalid stage` or `Invalid scene`.

This is a debugging checkpoint facility, **not a full save state**. Scene IDs are script boundaries, not labeled or validated checkpoints. Existing Arcade-script entries are checked before use; unavailable entries fall back to a normal chapter-1 start and log rejection. Mid-scene starts run the scene's native variant-0 setup script before its variant-1 gameplay script when available, loading its section/resources through the normal event system. Chapter entrances retain the native variant choice. Valid scripts can still depend on earlier flags, actors or route choices, or lack complete setup. Those prerequisites and full chapter/scene coverage need runtime qualification. Failed writes retain the previous file and retry after five seconds; startup and save results appear in `HOTD2Bridge.log`.

### Pausing

A/X uses the game's normal pause. Both the VR view and the injected game's desktop window retain the last complete 3D scene and HUD, without the native pause backdrop or pause text. Head and controller tracking continue in VR, so you can look around the frozen scene. Press the same player's A/X again to resume.

The desktop window redraws the retained geometry and native textures through Direct3D 7; it does not display a saved screenshot. This requires the injected bridge and a prior gameplay scene. Rendering continues while paused, but gameplay stays frozen. The native desktop view does not apply the viewer's brightness, gamma or world-scale settings.

The bridge and viewer must both be rebuilt for the current protocol 18; pause state was introduced in protocol 13, audio controls in 14, briefing-book provenance in 15 and confirmed shot events in 16. Protocol 18 combines separate VR Pause and Reload/Cancel inputs with player-HUD ownership; it rejects the incompatible protocol-17 builds from the separate branches. Details and verification: [docs/pause.md](docs/pause.md).

### Two hands and akimbo

Right always controls P1; left always controls P2.

- `akimbo=1`: both guns appear from startup. Either hand's Start requests both players, so one menu confirmation joins both.
- `akimbo=0`: each hand joins its own player. Gun visibility follows native player status, including accepted joins waiting through a cutscene. A rejected join, continue countdown, game over or return to menus does not leave an inactive gun visible.

Firing and tracking loss do not swap player identities. A hidden hand's held trigger must be released before it can fire after joining. Recenter and menu navigation remain available before joining.

## Configuration

Edit **`build\HOTD2VR.ini`**, beside the executables. The root `HOTD2VR.ini` supplies defaults for a new build directory; later builds preserve the active file. Command-line switches override the INI for that launch without saving changes.

Restart the viewer for `[vr]` changes. Restart the injected game for `[bridge]` changes.

### Audio volume

```ini
[vr]
master_volume=100
music_volume=100
effects_volume=100
```

All three default to 100. Values are integer percentages from 0 to 100: 0 mutes and 100 keeps the original level. `music_volume` controls background music; `effects_volume` includes gunshots, voices and other effects. Master and category gains multiply: master 50 with music 40 gives music 20% of its original level. These controls affect the game's DirectSound audio; Windows and other applications stay unchanged.

For music and effects muted, without changing the INI:

```powershell
.\Start-HOTD2.ps1 -MusicVolume 0 -EffectsVolume 0
.\build\HOTD2VR.exe --music-volume 0 --effects-volume 0
```

Use `-MasterVolume N` or `--master-volume N` for the overall game volume. The PowerShell launcher forwards volume overrides only when explicitly supplied, preserving INI settings otherwise. Restart the viewer after changing the INI; matching audio-capable bridge/viewer builds are required. Missing or invalid INI values use 100; invalid CLI values are rejected, and the last valid CLI override wins. Overrides reach the game when the viewer connects and remain for that game process. Build/offline checks pass; audible game/headset verification remains pending.

### World scale

```ini
[vr]
worldscale=1.25
```

World scale is a multiplier: **1** restores the previous size, **1.25** makes the world 25% larger, **0.8** makes it 20% smaller, and **2** doubles it. Allowed range: 0.1–10. Missing or invalid values fall back to 1.25. Convert older percentage settings by dividing by 100 (`140` becomes `1.4`).

Use `Start-HOTD2.ps1 -WorldScale 1.25` or `HOTD2VR.exe --world-scale 1.25` for a session override. Geometry and controller-ray conversion share the scale; HUD/menu size and controller gun size stay fixed. `[bridge] units_per_metre` remains the base conversion from native game units, not this multiplier.

### Laser and crosshair

```ini
[vr]
laser=1
crosshair=1
```

Both default on, including when the keys are absent. Each setting applies to both hands. `laser=0` hides the beam and muzzle flare; `crosshair=0` hides the VR aiming ring, core and ticks. Aiming, firing and gun models remain active.

Session overrides:

```powershell
.\Start-HOTD2.ps1 -GameExe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' -NoLaser -NoCrosshair
.\build\HOTD2VR.exe --no-laser --no-crosshair
```

Use `-Laser` / `-Crosshair` or `--laser` / `--crosshair` to enable either over a disabled INI setting. Direct CLI flags use the last supplied value. The PowerShell script rejects contradictory switches.

Native gameplay crosshairs are omitted automatically from converted geometry for both players and all four native styles. Options previews and native desktop rendering stay intact. These switches control only the generated VR aiming graphics. The legacy positional `hidecross` setting is no longer used; existing copies are ignored. A native framebuffer panel still mirrors the desktop pixels, including its crosshair.

### Advanced graphics

```ini
[vr]
native_lighting=1
```

Native lighting defaults on. It reproduces the game's lights, ambient/material shading, specular highlights and fog. Set `native_lighting=0` to restore the earlier texture/material-color rendering and bypass CPU light queries, normal preparation and per-vertex lighting. This disables the whole lighting conversion, including specular and fog; menus and HUD retain their native colors.

Session overrides (INI remains unchanged):

```powershell
.\Start-HOTD2.ps1 -GameExe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' -NoNativeLighting
.\build\HOTD2VR.exe --no-native-lighting
```

Use `-NativeLighting` or `--native-lighting` to force it on. Last direct CLI switch wins. Restart/reconnect the viewer to apply a change; the injected game's native desktop rendering stays unchanged. Rebuild and restart both bridge and viewer together when upgrading to the lighting-capable build.

Lighting adds CPU work; actual VR frame-time cost remains unmeasured. Disabling it skips those calculations but retains the same transport format. MSAA, supersampling, gamma, depth and sharp HUD filtering are already available; see the settings table below.

Scene brightness is separate from gamma: `[vr] brightness=100` is neutral; integer values 25–400 darken or brighten the converted scene. Brightness defaults to **90%**. Missing or invalid INI values use 90. Gain applies after fog/specular and before sRGB gamma compensation, with native lighting on or off. HUD, gun/aim graphics, the captured-screen panel and native desktop game stay unchanged.

For 80% scene brightness, without changing the INI:

```powershell
.\Start-HOTD2.ps1 -Brightness 80
.\build\HOTD2VR.exe --brightness 80
```

Restart the viewer to apply it. The last direct CLI value wins; invalid CLI values are rejected.

These controls already cover VC2VR's main picture settings. Its extra scene-reconstruction passes and backdrop fixes are game-specific. Configurable HUD size/distance remains a possible follow-up; a settings menu is not implemented yet.

### Visibility expansion

```ini
[vr]
expand_meshes=1
expand_world=1
```

`expand_meshes=1` preserves the existing normal-model screen-edge expansion, helping retain offscreen body parts. Set it to `0` to restore native normal-model mesh rejection. Alternative-model draws retain their native visibility checks by default.

`expand_world=1` enables experimental capture of loaded static stage scenery beyond the original camera, including behind it. It follows stage submissions through their deferred pass and relaxes original-camera near/far rejection only for those stage meshes. Custom clip planes, face culling and the viewer's depth buffer remain active. This is not a scenery cache: unloaded sections, other object-level omissions and surfaces the game never supplied can still leave gaps. Extra submissions can increase CPU work and geometry/texture pressure. The shipped INI enables world expansion for new players; existing INIs stay unchanged, and a missing key still falls back to off. Earlier headset testing captured rear scenery but showed jerkiness and artifacts. Full rear coverage and forward-wall parity remain unverified.

Session overrides, without changing the INI:

```powershell
.\Start-HOTD2.ps1 -ExpandWorld
.\build\HOTD2VR.exe --expand-world
```

Use `-ExpandMeshes` / `-NoExpandMeshes` and `-ExpandWorld` / `-NoExpandWorld`, or viewer flags `--expand-meshes` / `--no-expand-meshes` and `--expand-world` / `--no-expand-world`. Last direct CLI switch wins; PowerShell rejects opposing switches. Restart the viewer to apply changes. Matching bridge/viewer builds are required.

### Other shipped settings

These are the values in the shipped INI; an existing active file can differ.

| Section/key | Value | Purpose |
| --- | --- | --- |
| `[vr] akimbo` | `1` | Paired joining; `0` uses independent player joining |
| `[vr] gunmodel` / `gunscale` | `gun.obj` / `100` | Gun asset and its size in percent |
| `[vr] depth` / `depthbias` | `2` / `0` | Reconstructed depth buffer and optional sort bias |
| `[vr] expand_meshes` / `expand_world` | `1` / `1` | Normal-model side expansion; experimental loaded-stage rear capture |
| `[vr] msaa` | `4` | Antialiasing samples: 1, 2, 4 or 8 |
| `[vr] supersample` | `100` | Eye-buffer resolution in percent; separate from world size |
| `[vr] gamma` | `220` | sRGB eye-buffer compensation in percent; `100` disables it |
| `[vr] brightness` | `90` | Converted-scene brightness in percent, 25–400; HUD and guns unchanged |
| `[vr] master_volume` / `music_volume` / `effects_volume` | `100` / `100` / `100` | Game audio percentages, 0–100; effects include voices |
| `[vr] hudsharp` | `1` (if absent) | Sharp HUD texture filtering |
| `[vr] haptic` / `focusgame` | `1` / `1` | Shot feedback and initial native-window focus |
| `[vr] autozoom` | `0` | Automatic cinematic zoom disabled in the shipped configuration |
| `[bridge] windowed` / `background` | `1` / `1` | Native windowed mode and rendering while another window has focus |
| `[bridge] units_per_metre` | `10` | Initial native-unit conversion; headset calibration remains necessary |

See the commented [default INI](HOTD2VR.ini) for the complete shipped configuration. Relative gun-model paths resolve beside the viewer.

### Startup splashes and native window

`[bridge] skip_splash=1` skips the SEGA and Empire logos and starts at the title. Missing keys also default to on. Set `skip_splash=0`, use `Start-HOTD2.ps1 -ShowSplash`, or pass `--show-splash` to `HOTD2Launch.exe` to restore them. `-SkipSplash` / `--skip-splash` enables the bypass for one launch. Direct launcher flags also work with `--wait` or `--native-window`; the last splash flag wins.

The launcher validates the startup instruction and changes one byte while the process is suspended, preserving normal initialization. The bridge selects native 640×480 windowed mode, keeps the game rendering in the background, and accepts the known renderer-selection and quit-confirmation dialogs. Back retains its native action; quit confirmation is accepted whenever the game asks it. Other dialogs retain normal behavior.

For a native windowed comparison with the original splash sequence, without the capture bridge or viewer:

```powershell
.\build\HOTD2Launch.exe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' --native-window --show-splash
```

Native renderer and quit questions remain in this comparison mode. Starting `Hod2.exe` normally retains its original behavior.

## Optional cheats

All default off. Enable for a session:

```powershell
.\Start-HOTD2.ps1 -GameExe 'C:\Games\TheHouseOfTheDead2\Hod2.exe' -InfiniteLives -InfiniteAmmo -Invincibility -InfiniteContinues -RapidFire -RapidFireRate 4
```

Use whichever flags you want. For a viewer reconnect, use `--infinite-lives`, `--infinite-ammo`, `--invincibility`, `--infinite-continues`, `--rapid-fire` and `--rapid-fire-rate 4`.

| Cheat | INI key under `[vr]` | Effect |
| --- | --- | --- |
| Infinite lives | `infinite_lives=1` | Prevents life loss and the damage handler's score/rank penalty for both players. |
| Infinite ammo | `infinite_ammo=1` | Prevents ammunition consumption. Reload once if the gun is already empty when enabling it. |
| Invincibility | `invincibility=1` | Blocks native player damage, hit reactions and camera shake for both players. Enemies still animate attacks. |
| Infinite continues | `infinite_continues=1` | Enables native free play: players can start or continue without needing or spending credits. Death, the continue countdown and Start confirmation retain their normal flow. |
| Rapid fire | `rapid_fire=1` | Repeats a held VR trigger, using `rapid_fire_rate` shots per second. |

Rapid fire defaults to **4 shots/sec**; whole-number rates 1–30 are accepted. Native timing and weapon handling can lower the actual rate. Reload gestures take priority; menus, pause, cutscenes, inactive players and stale input receive no repeated shots. The rate override does not enable rapid fire by itself. Visual recoil retains its trigger-press behavior. Rumble follows confirmed native shots for each player, including Original-mode weapons: one 30 ms pulse at the existing fixed strength, capped at 10 pulses/sec per controller. The cap never changes firing rate. Excess feedback is dropped without a backlog; empty pulls, reload gestures, menus, pause and tracking loss produce no firing rumble. `haptic=0` disables it. Headset feel and performance remain to be verified.

While the viewer heartbeat is fresh, lives/ammo and free-play settings override the native settings: `0` forces off, `1` forces on, for both players. Changing an option to `0` does not restore a pre-enabled native cheat. The bridge saves prior native values when the viewer takes ownership, restores them on viewer exit or heartbeat expiry (500 ms), and captures them again on reconnect. Invincibility off lets native damage run normally. Credit counts are not rewritten, and the bridge does not edit save files. Unsupported instruction signatures leave the affected feature unavailable and produce a log entry. Rebuild and restart both game and viewer together when updating cheat support. Full-game attack and continue coverage still needs runtime qualification.

Implementation and test evidence: [docs/cheats.md](docs/cheats.md).

## How it works

1. The x86 launcher creates `Hod2.exe` suspended, applies selected startup changes, injects `HOTD2Bridge.dll`, then resumes the game.
2. The HOTD2 bridge hooks DirectDraw/Direct3D7 submissions. It captures geometry, textures and the live projection, reconstructs pretransformed vertices, and retains native material, blending, alpha-test and culling information.
3. Coherent frames and a texture atlas pass through shared memory to the separate x64 Direct3D11/OpenXR viewer.
4. The viewer renders the scene for both eyes. Controller rays intersect world geometry; the hit projects through HOTD2's camera back into each player's native gun input. HUD and shot-effect geometry are excluded from aim intersections.

This differs from VC2VR's `HGL_VIEW.DLL` integration and extra rotated-camera passes. HOTD2 uses an injected capture bridge with configurable mesh/stage visibility expansion. Experimental `expand_world` broadens loaded stage submissions without rotating the camera or retaining scenery across frames. Full 360-degree coverage is not established: other culling, unloaded sections and never-authored surfaces can still leave gaps when you turn or lean.

The bridge also repairs alpha after native bitmap uploads so menu art remains visible. Screen-space UI is captured in every game mode, including tutorial, story, game-over and menus, without a native depth cutoff. It uses a canvas 2 m away and 2 × 1.5 m in size, anchored to the recenter origin. Native transparency and submission order remain; UI draws above world geometry and never becomes an aim or splash target. The captured-screen panel replaces that HUD layer when its texture is available, avoiding duplicate UI. Model geometry, including untagged traveling tracers, keeps its world depth. Native letterbox bars are omitted from VR capture. During pause, VR retains the last gameplay scene; full-screen blackouts outside pause remain captured. These capture changes leave the native game rendering path running.

Texture capture preserves each material's independent U/V wrap, mirror and clamp modes. World and HUD shaders apply addressing after UV interpolation, preserving the native mapping on mirrored model halves. Opening-car material inspection and offscreen viewer replay passed; boss/headset appearance still needs confirmation. Rebuild and restart both bridge and viewer after capture-format changes.

## Troubleshooting and current limits

- **Unsupported executable:** compare the game's hash with the supported build above. This is an exact-build integration.
- **No bridge / protocol mismatch:** launch through `Start-HOTD2.ps1`, close stale game/viewer sessions, and rebuild the bridge and viewer together. A viewer alone cannot inject an already running unmodified game.
- **OpenXR fails but desktop preview works:** check the active runtime, headset connection and x64 loader beside the viewer.
- **Unexpected settings:** check `build\HOTD2VR.ini`, not only the root template. Rebuilding preserves existing values.
- **Missing surfaces or rendering glitches:** capture cannot supply geometry the game never submits. Texture-atlas capacity, mutable textures, transparency ordering and broader scene coverage remain areas for qualification.

Core 3D rendering, startup centering, controller shooting, close aiming, reload, the mode-selection menu, gameplay HUD placement and letterbox removal have user headset confirmation. World-scale changes, laser/crosshair toggles, independent-player presentation, exact reload thresholds, broader UI/mode coverage, stereoscopic comfort and full-game compatibility still need further headset checks. Native cheat activation/restoration passed desktop checks; damage protection, P2 gameplay and Original-mode weapons still need runtime confirmation. Offline or desktop success does not establish those results. Detailed evidence and remaining checks live in [docs/findings.md](docs/findings.md).

## Validation

`build.ps1` runs offline checks for capture/projection, native input signatures and trigger edges, startup patches, cheat restoration and rapid-fire guards, texture-cache behavior, coherent frame snapshots, scale and indicator options, and offscreen Direct3D11 rendering. These checks start neither the game nor OpenXR.

With an injected game already running:

```powershell
python .\tests\test_live.py
python .\tests\test_live.py --input
```

Close the viewer before the `--input` check. It briefly drives both aim coordinates without firing, then checks watchdog expiry and the native axis-write instructions. Logs and diagnostic images stay under ignored `build\`.

## Removal and rollback

Close the viewer and game. All hooks and startup changes disappear with the game process. Launch the original `Hod2.exe` normally to play without the mod; the mod does not deploy DLLs into its folder or edit `Hod2.ini`. The development backup `build\Hod2.ini.before-test`, when present, predates the first desktop test.

## Credits

- **[NeuralF — Virtua Cop 2 VR / VC2VR](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR):** the viewer foundation, OpenXR/shared-memory approach, controller handling, gun rendering, recoil and sight code. The upstream MIT copyright notice is retained in [LICENSE](LICENSE).
- **Gun assets bundled with VC2VR:** the Emerald revolver OBJ, material and texture, reused for both hands. The [upstream model header](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR/blob/d99da1e6795ead74ac1bd9f24eec1a89e414ca62/gun/gun.obj) labels it “Emerald revolver (Meshy)”. No separate artist name or original model-page link was found in the checked upstream files. See [asset provenance](THIRD-PARTY.md#gun-model-and-texture).
- **[The Khronos Group](https://github.com/KhronosGroup/OpenXR-SDK):** OpenXR headers and loader.
- **[Argonlefou / DemulShooter](https://github.com/argonlefou/DemulShooter):** HOTD2 input-location and keyboard-convention reference, cross-checked against the supported executable. No DemulShooter code is included.

Full component and asset notes: [THIRD-PARTY.md](THIRD-PARTY.md). The House of the Dead 2 and its game imagery belong to their respective owners; this independent fan mod is not affiliated with or endorsed by Sega. No HOTD2 or Virtua Cop 2 game files are distributed here.

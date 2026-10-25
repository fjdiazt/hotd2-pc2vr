# Technical reference

Installation, controller bindings and menu options are in the [README](../README.md).
This reference covers command-line use, configuration and implementation details.
Run command examples from the mod folder unless the section says otherwise.
Build scripts and developer tests require a source checkout.

- [Requirements and supported executable](#requirements)
- [Launching from another location](#download-and-run)
- [Building and packaging](#build-from-source)
- [Desktop previews and reconnecting](#desktop-previews-and-reconnecting)
- [Input integration](#controls)
- [Save game and scene selection](#save-game-and-scene-selection)
- [INI configuration and command-line overrides](#configuration)
- [Cheat configuration](#optional-cheats)
- [Architecture](#how-it-works)
- [Diagnostics and validation](#validation)

## Features and integration

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
- Release ZIPs need no PowerShell, Python or compilers.
- The x64 `openxr_loader.dll` beside the viewer (included in release ZIPs).

Only this `Hod2.exe` build is currently supported:

```text
SHA256 C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A
MD5    97C9E516A287AAB33A455A396DADAA45
```

The launcher checks the SHA256 before starting a game; native hooks also check their instruction signatures. Other executable layouts, the Remake, and emulated versions are outside the supported target.

## Download and run

Follow [How to play](../README.md#how-to-play) for installation. Each release ZIP contains one `vr` folder with the matching executables, bridge, default settings, gun assets, x64 OpenXR loader, documentation and licenses. The `.zip.sha256` file contains the ZIP checksum.

`HOTD2VR.exe` validates the game and extracts the settings font on first run. No scripts are needed to play. Keep the packaged runtime files together.

Game discovery starts beside the launcher and checks each parent folder for `Hod2.exe`, choosing the nearest copy. It does not depend on the terminal's current folder. An unsupported nearest executable stops launch rather than falling back to another installation.

For a mod installed elsewhere, supply the path explicitly:

```bat
HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe"
```

Examples below run from the mod folder. For source builds, run the executable under `build\` instead.

## Build from source

Source builds require 32-bit MinGW GCC (`i686-w64-mingw32`) and Visual Studio C++ x64 build tools with a Windows SDK. Python 3 is used only by developer tests and optional diagnostic tools.

From the repository root:

```powershell
.\build.ps1
.\build\HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --vrwindow
```

Replace the example path with your game location when running from a separate source checkout.

The build produces `HOTD2VR.exe`, `start.bat`, `injector.exe`, `HOTD2Bridge.dll`, the gun assets and `HOTD2VR.ini` under `build\`. Compiler outputs, test executables and diagnostic images stay under `build-artifacts\`. It runs offline checks before publishing the runtime files. It copies an OpenXR loader from the configured local VR installation when available; otherwise place the official x64 loader in `build\` yourself.

Close any existing game session before launching. Keep the bridge and viewer from the same build together; their shared-memory formats must match.

### Automated releases

Publishing a GitHub release, including a prerelease, runs `.github/workflows/release.yml` against its tagged commit. It builds on Windows, runs offline and packaging tests, creates the runtime ZIP and checksum, and attaches both to that release. Draft releases do not trigger a build. Assets appear after the workflow succeeds; a failed build leaves the release without new binaries.

The workflow also supports **Actions → Build release → Run workflow** for an inspection build. Manual runs save workflow artifacts and do not publish a release. Tag names used for packaging must start with a letter or digit and contain only letters, digits, dots, underscores or hyphens (maximum 80 characters). Reruns do not overwrite existing release assets.

To package a local build:

```powershell
.\package.ps1 -Version v1.0.0
```

The packager downloads Khronos OpenXR.Loader 1.1.65, verifies its pinned SHA256, checks binary architectures, and writes to `build-artifacts\releases\`. It packages only listed runtime files and fresh repository defaults; local INI edits, generated fonts, checkpoints, logs and test executables are excluded. Offline/CI checks do not prove headset rendering or controller behavior.

### Desktop previews and reconnecting

```powershell
.\HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --window --no-vrwindow
.\HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --stereo --no-vrwindow
```

`--window --no-vrwindow` shows the reconstructed scene on the desktop; `--stereo` shows both eyes side by side. These help distinguish capture/rendering problems from OpenXR problems.

Running `HOTD2VR.exe` while the bridge is active reconnects the viewer. Optionally use `--window --no-vrwindow` or `--stereo`. Closing the viewer leaves the game running; closing the connected game ends the viewer. `--seconds 20` limits the viewer session without closing the game.

Desktop preview controls: W/A/S/D move, mouse drag turns, R resets, M cycles the floating panel through auto/on/off, and Escape closes the viewer when VR Settings is closed. F8 toggles VR Settings; arrows navigate, Enter confirms and Right Ctrl goes back/closes.

Under `[vr]`, `window` controls the existing reconstructed desktop preview and `vrwindow` controls headset rendering independently. The native game window stays available in every combination; `[bridge] windowed` only selects its windowed/fullscreen mode.

| `window` | `vrwindow` | Outputs besides native game window |
| --- | --- | --- |
| `0` | `0` | Native graphics with checkpoint, cheat and audio services; no capture hooks |
| `1` | `0` | Desktop preview |
| `0` | `1` | Headset (default) |
| `1` | `1` | Desktop preview and headset |

CLI overrides: `--window` / `--no-window`, `--vrwindow` / `--no-vrwindow`. For both outputs, use `HOTD2VR.exe --window --vrwindow`. With no output switches, the INI selects both outputs. `--stereo` remains a separate desktop diagnostic mode.

The launcher applies these same flags before bridge injection. Both outputs off skips graphics capture, lighting reconstruction, culling expansion and native pause replay while retaining checkpoint, cheat and audio services. `HOTD2VR.exe` then runs its existing service loop without D3D11/OpenXR or synthetic aiming. Changing whether capture hooks are installed requires a game restart through the launcher; reconnecting a viewer alone cannot remove or install those hooks.

With both outputs enabled, the preview uses the existing independent desktop camera and runs read-only; only the VR viewer sends controller input, cheats and audio controls. Closing the preview leaves the headset and native game running. Closing the VR viewer closes its preview; closing the native game ends both. This is not a left-eye mirror. Running both renderers costs additional CPU/GPU work.

## Controls

See [Buttons you need](../README.md#buttons-you-need) for controller bindings.

Trigger supplies native Start and Fire; A/X supplies Start and Pause. The pause reader accepts A/X press edges and ignores synthetic trigger Start, preserving native pause eligibility, player ownership and device-disconnect behavior. Physical Enter/Tab retain native Start/Pause. Matching protocol-25 bridge/viewer builds are required.

Gesture reload uses the tracked gun's direction relative to the headset: up **50°**, down **45°**, or sideways **60°**, then trigger. Scripted camera motion and recenter offsets do not change these thresholds. Each pull stays a shot or reload until release; tracking loss disarms it until release. Two signature-checked weapon reads separate Grip Reload from B/Y Skip/Cancel while preserving native reload rules and physical Ctrl behavior.

### Player HUD placement

`[vr] swap_player_hud=1` (default) moves converted gameplay ammo, lives and join/credit rows to match the fixed gun assignments: P1 on the right, P2 on the left. Player labels, Original-mode weapon counts and reload messages/hints move with their row. The inactive player's PRESS START/INSERT COIN prompt and CREDIT/FREE PLAY indicator follow that player's side. Text, digit order, artwork, animation and HUD depth stay intact. Each row is aligned to the opposite side using its complete bounds; its width can vary with ammo/lives or messages.

Set `swap_player_hud=0` or use `--no-swap-player-hud` to retain the native layout. `--swap-player-hud` enables it. The last CLI switch wins. The flag works independently of `akimbo` and never changes controller/player assignments.

Ownership comes from the native player ammo/lives draw routines and gameplay credit-line routine, including deferred sprite copies. The global attract-screen PRESS START prompt, shared artwork elsewhere, captions, menus, world geometry and briefing-book geometry are retained. This change affects converted HUD geometry in the headset/desktop preview. The native game window and native framebuffer panels keep their original pixels and layout. Matching protocol-25 bridge/viewer builds are required. Ammo/lives placement with akimbo and the gameplay join/credits swap are user-confirmed in VR. Full mode, transition and authored-HUD coverage remains unverified.

### Save game and scene selection

Save game records the active **Arcade or Original** mode. Load game and Stage select first offer **Arcade / Original**; Stage select then offers chapter and scene. The mode chooser preselects the active supported mode during gameplay, otherwise Arcade. Browsing does not change the running game; confirming Load or a scene starts the transition. Training and Boss use their native selectors and cannot create mod checkpoints.

Original always starts through native item selection. Choose equipment again, then the game enters the requested scene. Equipment is neither saved nor restored by the mod. The pending transition does not time out while choosing items; VR Settings closes so it cannot block that selection.

CLI checkpoint commands default to **Arcade**. Use `--game-mode original` explicitly for Original:

```bat
HOTD2VR.exe --start-stage 4 --start-scene 0
HOTD2VR.exe --game-mode original --start-stage 4 --start-scene 0
HOTD2VR.exe --save
HOTD2VR.exe --game-mode original --save
HOTD2VR.exe --load
HOTD2VR.exe --game-mode original --load
```

`--save` requires a running injected game of the requested mode. Load and stage commands start the game if needed; against an existing bridge they send one command, wait for its result and exit without opening another renderer or taking over input, audio or cheats. Do not combine Save/Load with stage selection. Omitted `--start-scene` uses the chapter entrance (scene 7 for chapter 3, scene 0 otherwise). Invalid modes and destinations are rejected. A plain launch still uses the normal game menus; `--game-mode` identifies checkpoint commands, independently of VR output flags.

Enable `[vr] continue_game=1` to record and resume each mode's latest scene on subsequent native starts. Default is `0`. `--continue-game` / `--no-continue-game` override this for one launch. Explicit stage/scene selection takes priority over Autosave. Autosave excludes Original's item-selection screen.

Load and Stage select rebuild through native mode/task/resource initialization, resuming native pause when necessary. They preserve joined players; loading from a menu joins P1, or both players with Akimbo enabled.

**Game Settings > Autosave** updates `continue_game` live. Save/Load destinations refresh while the menu is open; requests show progress until the bridge acknowledges a result. Missing or invalid saves leave the current game intact. Manual Save/Load work with Autosave disabled.

The checkpoint API uses `checkpoint_action` (`HOTD_CHECKPOINT_SAVE`, `HOTD_CHECKPOINT_LOAD`, `HOTD_CHECKPOINT_TITLE` or `HOTD_CHECKPOINT_SELECT`) and an incremented nonzero `checkpoint_request`, published after a memory barrier. SAVE/LOAD/SELECT supply `checkpoint_target_mode` (0 Arcade, 1 Original); SELECT also supplies `checkpoint_target_stage` and `checkpoint_target_scene`. Replies include `checkpoint_mode`. `checkpoint_waiting_items` reports native Original item selection. Wait for the matching `checkpoint_response` before reading the result and destination. The legacy `HOTD_CHECKPOINT_NEW` command remains accepted for compatibility. Serialize callers; companion previews map the frame read-only. The game thread handles file writes and transitions while the viewer polls. Load acknowledgment waits for destination activation, with a 30-second bridge timeout, suspended during Original item selection. The viewer reports an unknown result after 45 seconds outside item selection without releasing request ownership. Heartbeat-owned `checkpoint_autosave` uses 0 for launch configuration, 1 for off and 2 for on; disconnect restores launch configuration.

The bridge stores `[checkpoint] version=2`, `mode=arcade|original`, `stage=1..6` and `scene=0..255` beside its DLL. **`HOTD2VR.checkpoint.ini`** holds Arcade; **`HOTD2VR.original.checkpoint.ini`** holds Original. Existing version-1 saves remain readable as Arcade. The files are separate from configuration and native game saves. Each write uses a flushed temporary file and atomic replacement. Autosave writes only when the supported mode/scene changes. Player health, score, equipment, inventory, enemies, route history and exact script position are not saved. Disabling Autosave retains both files; manual Save/Load remain available.

Direct launcher equivalents: `injector.exe path-to-Hod2.exe --start-stage 6 --start-scene 3 --continue-game`; `--no-continue-game` disables recording/resume. Add `--game-mode original` for a native Original startup destination. Unlike the executable's live stage command, helper startup flags apply at the next new game through the normal menus. `--native-window` skips bridge injection and cannot be combined with a checkpoint request. Stage/scene choices are launch options or menu actions; only Autosave is stored as an INI preference.

Both executables reject missing or invalid stage/scene values before starting a process.

This is a debugging checkpoint facility, **not a full save state**. Scene IDs are script boundaries, not labeled or validated checkpoints. Startup scene entries are checked before use; unavailable startup entries fall back to a normal chapter-1 start and log rejection. Live Load validates the saved destination against bounded, relocated native event tables on disk before requesting a transition; invalid destinations report failure without unloading the current scene. Mid-scene starts run the scene's native variant-0 setup script before its variant-1 gameplay script when available, loading its section/resources through the normal event system. Chapter entrances retain the native variant choice. Valid scripts can still depend on earlier flags, actors or route choices, or lack complete setup. Those prerequisites and full chapter/scene coverage need runtime qualification. Failed writes retain the previous file and retry after five seconds; startup and save results appear in `HOTD2Bridge.log`.

### Pausing

A/X pauses or resumes with Settings open or closed. VR and the injected desktop window retain the last complete scene and HUD, hiding the native pause backdrop/text. Head and controller tracking continue so you can look around the frozen scene.

Desktop pause redraws retained geometry and textures through Direct3D 7. It needs an injected bridge and a previously rendered gameplay scene; the native view uses its own picture settings.

### VR Settings

See [VR Settings menu](../README.md#vr-settings-menu) for navigation and [Settings explained](../README.md#settings-explained) for every option. The native-font panel has a 50% opaque background. It consumes menu input before firing, reload, join or skip; A/X pause and recenter remain available.

Live options update the viewer and bridge heartbeat. Brightness/Gamma rebuild their shaders; lighting and visibility flags affect subsequent captured frames. The menu identifies viewer/game restart requirements. Gun model paths remain INI-only.

Edited settings save to `HOTD2VR.ini` beside the viewer when the panel closes or the viewer exits normally. Default values remove their keys. Saves atomically replace the INI and keep timestamped `.menu-*.bak` backups; untouched keys and comments survive. A failed menu close leaves the panel open for retry. Opening and closing without edits writes nothing.

Reset defaults removes overrides for all menu options, including CLI overrides, when the panel closes. Hidden/unknown INI keys, gun paths and native game options are preserved. Return to title uses native mode 3 and closes the panel after acknowledgment. Quit game saves options and sends the verified native window its normal close request; an unavailable window or save failure leaves the panel open.

`[vr] menu=0` or `--no-menu` disables the panel; `--menu` enables it. A missing/invalid extracted `menu-font.rgba` also disables it with a log message. The original Game Options screen is separate.

Keyboard input works while the native game or viewer has focus. In addition to the controls above, Tab/Left Ctrl provide P2 confirm/back, and Backspace/Escape are back aliases. Keyboard Back closes the root page; controller B/Y stays at the root. Companion previews are read-only; focus the native game to edit during a combined session.

The panel renders in the native window, desktop preview and headset. Disabling both outputs also disables capture and Settings while retaining checkpoint, cheat and audio services. Native composition happens after capture to avoid a duplicate panel in framebuffer views.

Matching protocol-25 bridge/viewer builds are required. Protocol 25 retains command 3 to Return to title and command 4 to Stage select, rejecting older conflicting assignments. See [menu integration](vr-menu.md) and [protocol/pause notes](pause.md).

### Two hands and akimbo

Player assignments are fixed: right is P1, left is P2.

- `akimbo=1`: both guns appear from startup. Either hand's Start requests both players, so one menu confirmation joins both.
- `akimbo=0`: each hand joins its own player. Gun visibility follows native player status, including accepted joins waiting through a cutscene. A rejected join, continue countdown, game over or return to menus does not leave an inactive gun visible.

Firing and tracking loss do not swap player identities. A hidden hand's held trigger must be released before it can fire after joining. Recenter and menu navigation remain available before joining.

## Configuration

Edit **`HOTD2VR.ini`** beside the executables (`vr\HOTD2VR.ini` for releases; `build\HOTD2VR.ini` for source builds). The repository root INI supplies defaults for a new build directory; later builds preserve the active file. Command-line switches override the INI for that launch without saving changes.

After editing the INI, restart the viewer. Output flags (`window`, `vrwindow`) and `[bridge]` changes require a game restart. The last CLI switch wins.

### Audio volume

```ini
[vr]
master_volume=100
music_volume=100
effects_volume=100
```

Audio values are percentages from 0 to 100. Master and category gains multiply: Master 50 and Music 40 produce 20% of the original music level. These controls affect game DirectSound audio.

For music and effects muted, without changing the INI:

```powershell
.\HOTD2VR.exe --music-volume 0 --effects-volume 0
```

Use `--master-volume N` for overall volume. Missing or invalid INI values use 100; invalid CLI values are rejected. Values reach the game when the viewer connects and remain for that game process. Audible game/headset verification remains pending.

### World scale

```ini
[vr]
worldscale=1.25
```

World scale is a multiplier: **1** restores the previous size, **1.25** makes the world 25% larger, **0.8** makes it 20% smaller, and **2** doubles it. Allowed range: 0.1–10. Missing or invalid values fall back to 1.25. Convert older percentage settings by dividing by 100 (`140` becomes `1.4`).

Use `HOTD2VR.exe --world-scale 1.25` for a session override. Geometry and controller-ray conversion share the scale; HUD/menu size and controller gun size stay fixed. `[bridge] units_per_metre` remains the base conversion from native game units, not this multiplier.

### Laser and crosshair

```ini
[vr]
laser=1
crosshair=1
```

`laser=0` hides the beam and muzzle flare; `crosshair=0` hides the VR ring, core and ticks. Both settings affect both hands. Lasers/crosshairs use scarlet for P1 and cobalt for P2, with brighter firing feedback, regardless of the gun appearance setting.

Session overrides:

```powershell
.\HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --no-laser --no-crosshair
```

Use `--laser` / `--crosshair` to enable either over a disabled INI setting.

Native gameplay crosshairs are omitted automatically from converted geometry for both players and all four native styles. Options previews and native desktop rendering stay intact. These switches control only the generated VR aiming graphics. The legacy positional `hidecross` setting is no longer used; existing copies are ignored. A native framebuffer panel still mirrors the desktop pixels, including its crosshair.

### Advanced graphics

```ini
[vr]
native_lighting=1
```

`native_lighting=0` bypasses native light queries, normal preparation and per-vertex lighting, including specular and fog. Menus and HUD keep their native colors.

Session overrides (INI remains unchanged):

```powershell
.\HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --no-native-lighting
```

Use `--native-lighting` to enable it. Rebuild and restart both bridge and viewer when upgrading the capture format.

Lighting adds CPU work; VR frame-time cost remains unmeasured.

`brightness` scales scene/gun color after fog and specular, before sRGB gamma compensation. It applies with native lighting on or off; HUD, aim graphics and framebuffer panels use their own colors. See [Display](../README.md#display) for its range and default.

For 80% scene brightness, without changing the INI:

```powershell
.\HOTD2VR.exe --brightness 80
```

Invalid CLI brightness values are rejected.

### Visibility expansion

```ini
[vr]
expand_meshes=1
expand_world=1
```

`expand_meshes=1` preserves the existing normal-model screen-edge expansion, helping retain offscreen body parts. Set it to `0` to restore native normal-model mesh rejection. Alternative-model draws retain their native visibility checks by default.

`expand_world=1` captures loaded static scenery beyond the original camera by following deferred stage submissions and relaxing their near/far rejection. Custom clip planes, face culling and viewer depth testing remain active. Unloaded sections and surfaces the game never supplies can leave gaps. Extra submissions increase CPU and geometry/texture pressure. Earlier headset tests captured rear scenery with jerkiness and artifacts; full rear coverage and forward-wall parity remain unverified.

Session overrides, without changing the INI:

```powershell
.\HOTD2VR.exe --expand-world
```

Overrides: `--expand-meshes` / `--no-expand-meshes`, `--expand-world` / `--no-expand-world`. Matching bridge/viewer builds are required.

### Other shipped settings

INI keys for the options above, plus the gun asset path:

| Section/key | Value | Purpose |
| --- | --- | --- |
| `[vr] akimbo` | `1` | Paired joining; `0` uses independent player joining |
| `[vr] arcade_gun_colors` | `0` | Shaded arcade plastic instead of gun textures; live Controls toggle |
| `[vr] gunmodel` / `gunscale` | `gun.obj` / `100` | Gun asset and its size in percent |
| `[vr] depth` / `depthbias` | `2` / `0` | Reconstructed depth buffer and optional sort bias |
| `[vr] expand_meshes` / `expand_world` | `1` / `1` | Normal-model side expansion; experimental loaded-stage rear capture |
| `[vr] msaa` | `4` | Antialiasing samples: 1, 2, 4 or 8 |
| `[vr] supersample` | `100` | Eye-buffer resolution in percent; separate from world size |
| `[vr] gamma` | `220` | sRGB eye-buffer compensation in percent; `100` disables it |
| `[vr] brightness` | `90` | Converted-scene and gun brightness in percent, 25–400; HUD unchanged |
| `[vr] master_volume` / `music_volume` / `effects_volume` | `100` / `100` / `100` | Game audio percentages, 0–100; effects include voices |
| `[vr] hudsharp` | `0` | Sharp HUD texture filtering; explicit `1` enables it |
| `[vr] haptic` / `focusgame` | `1` / `1` | Shot feedback and initial native-window focus |
| `[bridge] windowed` / `background` | `1` / `1` | Native windowed mode and rendering while another window has focus |
| `[bridge] units_per_metre` | `10` | Initial native-unit conversion; headset calibration remains necessary |

The [shipped INI](../HOTD2VR.ini) provides release defaults, including the startup CD-check bypass. Relative gun-model paths resolve beside the viewer. Both gun styles use the viewer Brightness/Gamma settings. Arcade shells are untextured scarlet/cobalt plastic with local face shading and soft highlights; gun lighting does not sample level lights or shadows.

### Startup splashes and native window

`[bridge] skip_splash=1` starts at the title, skipping SEGA/Empire logos. Use `skip_splash=0` or `HOTD2VR.exe --show-splash` to restore them; `--skip-splash` enables the bypass. The internal `injector.exe` helper also accepts these flags with `--wait` and `--native-window`.

Release settings enable `[bridge] launchfix=1` to bypass the supported game's CD check in memory before startup. The injector checks the original instruction signature, changes one branch displacement, restores memory protection and verifies the result. Unsupported code stops launch. `Hod2.exe` stays unchanged; setting `launchfix=0` in `HOTD2VR.ini` restores the original check on the next launch. Installed game data is still required.

The launcher validates the startup instruction and changes one byte while the process is suspended, preserving normal initialization. The bridge selects native 640×480 windowed mode, keeps the game rendering in the background, and accepts the known renderer-selection and quit-confirmation dialogs. Back retains its native action; quit confirmation is accepted whenever the game asks it. Other dialogs retain normal behavior.

For a native windowed comparison with the original splash sequence, without the capture bridge or viewer:

```powershell
.\injector.exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --native-window --show-splash
```

For native graphics with no graphics hooks:

```powershell
.\HOTD2VR.exe --no-graphics-hooks --start-stage 4 --start-scene 0 --no-continue-game
```

The executable starts the selected Arcade scene; add `--game-mode original` to use Original and choose items first. Native keyboard/mouse controls remain available. Existing cheat and volume flags still work; ammo, lives, invincibility and free-play controls do not require conversion rendering. Rapid fire remains tied to VR trigger input and supplies no physical mouse auto-fire in this mode.

`--no-graphics-hooks` forces both conversion outputs off regardless of INI/output flags. Services run from the native input update, with no presentation, DirectDraw/Direct3D, model/texture, culling, lighting, pause-renderer hooks or framebuffer readback. The normal native windowed selection and optional CD/splash startup patches still apply. The existing `--no-window --no-vrwindow` combination uses the same services path.

Use `--continue-game` to resume the saved checkpoint. The shared-memory checkpoint API also works in this mode, but Settings is unavailable. Close the existing game before launching another session.

Direct bridge launch: `injector.exe path-to-Hod2.exe --no-graphics-hooks --start-stage 4 --start-scene 0 --no-continue-game`; attach `HOTD2VR.exe --no-window --no-vrwindow` to publish configured cheats/audio. `--native-window` remains the separate comparison mode without any bridge injection.

For the bat-wing investigation, optional `[bridge] trace_bats=1` records only chapter 4, scene 0 bat models `0x1B01..0x1B09` while unpaused. It buffers four frames at least 15 game frames apart, capped at 256 draw records, then writes `HOTD2BatTrace-<pid>-<tick>.bin` plus layout metadata `.json` and matching native-frame BMPs beside the bridge. Pause or scene exit flushes an already started partial trace. Each draw retains the native model record, matrices, material/lights, up to 256 input vertices and 768 converted vertices; truncation is marked. Capture-disabled launches install no trace. Default is off; no rendering state is changed by the trace. The final bounded write can cause a brief hitch, so this is diagnostic output rather than a performance benchmark. Inspect it with `python tools/read_bat_trace.py path-to-trace.json`.

Other diagnostics also default off: `[bridge] trace_render_states=1` enables render-state sampling (at most 32 distinct states, sampled every 30 frames), and `trace_input=1` enables per-shot/reload log lines. Restart the game to change them. Ordinary startup, failure and periodic health logs remain available. Native mode with graphics hooks disabled does no render-state sampling.

Native renderer and quit questions remain in this comparison mode. Starting `Hod2.exe` normally retains its original behavior.

## Optional cheats

All default off. Enable for a session:

```powershell
.\HOTD2VR.exe --game-exe "C:\Games\TheHouseOfTheDead2\Hod2.exe" --infinite-lives --infinite-ammo --invincibility --protect-rescue-npcs --infinite-continues --rapid-fire --rapid-fire-rate 4
```

Choose the flags you need.

| Cheat | INI key under `[vr]` |
| --- | --- |
| Infinite ammo | `infinite_ammo=1` |
| Infinite lives | `infinite_lives=1` |
| Invincibility | `invincibility=1` |
| Protect rescue NPCs | `protect_rescue_npcs=1` |
| Infinite continues | `infinite_continues=1` |
| Rapid fire | `rapid_fire=1` |
| Shots per second | `rapid_fire_rate=4` |

Rapid fire defaults to **4 shots/sec**; whole-number rates 1–30 are accepted. Native timing and weapon handling can lower the actual rate. Reload gestures take priority; menus, pause, cutscenes, inactive players and stale input receive no repeated shots. The rate override does not enable rapid fire by itself. Visual recoil retains its trigger-press behavior. Rumble follows confirmed native shots for each player, including Original-mode weapons: one 30 ms pulse at the existing fixed strength, capped at 10 pulses/sec per controller. The cap never changes firing rate. Excess feedback is dropped without a backlog; empty pulls, reload gestures, menus, pause and tracking loss produce no firing rumble. `haptic=0` disables it. Headset feel and performance remain to be verified.

While the viewer heartbeat is fresh, lives/ammo and free-play settings override the native settings: `0` forces off, `1` forces on, for both players. Changing an option to `0` does not restore a pre-enabled native cheat. The bridge saves prior native values when the viewer takes ownership, restores them on viewer exit or heartbeat expiry (500 ms), and captures them again on reconnect. Invincibility off lets native damage run normally. Credit counts are not rewritten, and the bridge does not edit save files. Unsupported instruction signatures leave the affected feature unavailable and produce a log entry. Rebuild and restart both game and viewer together when updating cheat support. Full-game attack and continue coverage still needs runtime qualification.

Implementation and test evidence: [docs/cheats.md](cheats.md).

## How it works

1. `HOTD2VR.exe` finds and validates the game, extracts the menu font if needed, and invokes the x86 launcher. The launcher creates `Hod2.exe` suspended, applies selected startup changes, injects `HOTD2Bridge.dll`, then resumes the game.
2. The HOTD2 bridge hooks DirectDraw/Direct3D7 submissions. It captures geometry, textures and the live projection, reconstructs pretransformed vertices, and retains native material, blending, alpha-test and culling information.
3. Coherent frames and a texture atlas pass through shared memory to the separate x64 Direct3D11/OpenXR viewer.
4. The viewer renders the scene for both eyes. Controller rays intersect world geometry; the hit projects through HOTD2's camera back into each player's native gun input. HUD and shot-effect geometry are excluded from aim intersections.

The upstream renderer uses `HGL_VIEW.DLL` integration and extra rotated-camera passes. HOTD2 uses an injected capture bridge with configurable mesh/stage visibility expansion. Experimental `expand_world` broadens loaded stage submissions without rotating the camera or retaining scenery across frames. Full 360-degree coverage is not established: other culling, unloaded sections and never-authored surfaces can still leave gaps when you turn or lean.

The bridge also repairs alpha after native bitmap uploads so menu art remains visible. Screen-space UI is captured in every game mode, including tutorial, story, game-over and menus, without a native depth cutoff. It uses a canvas 2 m away and 2 × 1.5 m in size, anchored to the recenter origin. Native transparency and submission order remain; UI draws above world geometry and never becomes an aim or splash target. The captured-screen panel replaces that HUD layer when its texture is available, avoiding duplicate UI. Model geometry, including untagged traveling tracers, keeps its world depth. Native letterbox bars are omitted from VR capture. During pause, VR retains the last gameplay scene; full-screen blackouts outside pause remain captured. These capture changes leave the native game rendering path running.

Texture capture preserves each material's independent U/V wrap, mirror and clamp modes. World and HUD shaders apply addressing after UV interpolation, preserving the native mapping on mirrored model halves. Opening-car material inspection and offscreen viewer replay passed; boss/headset appearance still needs confirmation. Rebuild and restart both bridge and viewer after capture-format changes.

## Troubleshooting and current limits

- **Unsupported executable:** compare the game's hash with the supported build above. This is an exact-build integration.
- **No bridge / protocol mismatch:** close stale game/viewer sessions and start `HOTD2VR.exe`. Keep bridge and viewer builds together; close an already running unmodified game first.
- **OpenXR fails but desktop preview works:** check the active runtime, headset connection and x64 loader beside the viewer.
- **Unexpected settings:** check `HOTD2VR.ini` beside the running executable. Source builds use `build\HOTD2VR.ini`, separate from the root template. Rebuilding preserves existing values.
- **Missing surfaces or rendering glitches:** capture cannot supply geometry the game never submits. Texture-atlas capacity, mutable textures, transparency ordering and broader scene coverage remain areas for qualification.

Core 3D rendering, startup centering, controller shooting, close aiming, reload, the mode-selection menu, gameplay HUD placement and letterbox removal have user headset confirmation. World-scale changes, laser/crosshair toggles, independent-player presentation, exact reload thresholds, broader UI/mode coverage, stereoscopic comfort and full-game compatibility still need further headset checks. Native cheat activation/restoration passed desktop checks; damage protection, P2 gameplay and Original-mode weapons still need runtime confirmation. Offline or desktop success does not establish those results. Detailed evidence and remaining checks live in [docs/findings.md](findings.md).

## Validation

`build.ps1` runs offline checks for capture/projection, native input signatures and trigger edges, startup patches, cheat restoration and rapid-fire guards, texture-cache behavior, coherent frame snapshots, scale and indicator options, and offscreen Direct3D11 rendering. These checks start neither the game nor OpenXR.

With an injected game already running:

```powershell
python .\tests\test_live.py
python .\tests\test_live.py --input
```

Close the viewer before the `--input` check. It briefly drives both aim coordinates without firing, then checks watchdog expiry and the native axis-write instructions. Logs and diagnostic images stay under ignored `build-artifacts\`.

For the chapter-4/scene-0 bat comparison, close game/viewer/VR sessions, then run:

```powershell
python tools/capture_bats.py --game 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe' --re-source 'C:\src\gaming\hotd2-re' --runtime build-artifacts/bat-trace-stage/build
```

This diagnostic build reuses the RE project's autoplay selector and gun calibration. It shoots opening enemies through normal gun input, permanently stops firing when live bats appear, and captures at a nearest-bat distance of 30 game units. It runs native graphics and desktop conversion separately with `launchfix=1`, never enables OpenXR, and closes its own game/viewers afterward. Native mode explicitly uses `--no-graphics-hooks` and takes its screenshot externally, without pausing or reading the game's D3D device. That screenshot can follow the actor snapshot by several game frames; it is not an exact-frame comparison. Each run has an isolated INI/checkpoint under `build-artifacts/bat-bot-*`; normal builds and game configuration stay untouched. Output includes native actor records/transforms, desktop screenshots, and converted-frame/atlas evidence. Both modes reached the bat capture in desktop testing; see [findings](findings.md#bounded-bat-autoplay-diagnostic-2026-10-22).

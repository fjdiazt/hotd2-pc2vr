# Native integration findings — 2026-10-17

## Supported executable

`N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe`, x86, SHA256 `C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A`. MD5 matches DemulShooter's original PC release. The original EXE and `Hod2.ini` hashes were unchanged after desktop testing. `build\Hod2.ini.before-test` is the original configuration backup.

## Native seams

Addresses below are absolute for image base `0x400000`. Hooks use RVAs plus the running image base and validate original instructions.

- `0x49E461`: `push 0x0B` selects entry 11 of the native video-mode table at `0x5988F0`. Entry 7 is 640x480, 32-bit windowed. Changing only the immediate selects that existing mode; no external DirectDraw wrapper is needed. Windowed launch was observed on the desktop.
- `0x4A52FD` through `0x4A5340`: native device-selection loop asks a Yes/No question when multiple graphics candidates exist. `AllDevicePass` does not suppress this. An exact `MessageBoxA` prompt recognizer selects the native default; unrelated error dialogs remain intact.
- `0x49E7BB` through `0x49E7D7`: native quit prompt calls `MessageBoxA` with flags `0x24`, title `House of the Dead2`, and text `Are you sure you want to quit?`. Only `IDNO` (7) cancels closing. The existing bridge import hook now returns `IDYES` for this exact prompt, covering desktop and VR-triggered game exits in injected sessions. Direct unmodified game launches retain their confirmation. The actual hook self-check covers quit acceptance, renderer selection and unrelated/null prompt fallback.
- `0x4A66E4`: native `WM_ACTIVATEAPP` handling. The guarded activation-branch patch preserves rendering when the VR viewer takes foreground focus.
- `0x4A59C0`, `0x4A5A00`, `0x4A5A40`: native XYZ triangle strips, transformed/lit strips, and XYZ triangle lists. These are the stable capture seams. Direct3D rewrites optimized draw-method slots after device setup, so hooking `DrawPrimitive` only at creation stopped capturing during gameplay. The copied 9-byte prologues contain no relative branches.
- `0x572304`: D3DX context `UpdateFrame` vtable slot, initially `0x4BA3FF`. This marks complete frames; publishing each `EndScene` emitted partial or empty frames.
- `0x40CBB0`: native gun-input update for both players and all selected control types. VR axes and trigger press edges are applied after native processing. Writing coordinates earlier allowed native recentering to erase them. The original keyboard aim function at `0x40CFE0` and its axis-write instructions at `0x40D1AD` and `0x40D1B7` remain unmodified.
- Player axes: P1 `0x9C8FC4/0x9C8FC6`, P2 `0x9C8FEC/0x9C8FEE`. Signed game coordinates are `x = screen_x - 320`, `y = 240 - screen_y`.
- P1 native gun state starts at `0x9C8FC0`, P2 at `+0x28`. Offset `0x10` marks valid aim; offset `0x14` is the shot press edge consumed by shooting code at `0x414C04`. The original keyboard-only trigger injection failed with P1's selected mouse control type (`0x9A5D85=1`), despite confirmed OpenXR haptics and IPC FIRE bits. Applying fire after native gun polling reaches the shared mouse/keyboard path. Held triggers produce one edge; release/repress produces another; physical shot edges are preserved.
- `DirectInputCreateEx` and keyboard `GetDeviceState` supply local reload/start/back keys. No global keyboard or mouse events are generated.

## Rendering

### Native screen-edge mesh culling

The supported executable's `Mdl_Draw` at `0x4A7EF0` calls Direct3D7 `ComputeSphereVisibility` for individual meshes (`0x4A7F63`), then tests the result at `0x4A7F81` against `0x00FFF000`. A mesh rejected here never reaches polygon capture. This can hide a character's separate body meshes outside the original flat camera's field of view, even when those meshes should be visible in VR.

The bridge validates the eight-byte instruction signature and changes only `0x4A7F86`, from `F0` to `00`: the rejection mask becomes `0x00FF0000`. This clears only the LEFT/RIGHT/TOP/BOTTOM sphere-intersection reject bits. FRONT/BACK and six custom planes remain; material passes, backface behavior and damage/dismemberment visibility are untouched. Whole-object/stage culling elsewhere remains. The change affects injected sessions in memory; the game executable on disk is unchanged. The signature self-check verifies preserved planes, unchanged surrounding bytes and rejection of unsupported instructions.

This describes the original fixed-mask implementation. The centralized visibility policy documented below replaces that byte patch; the measurements in this section remain evidence for the earlier implementation.

Full build/offline checks passed (`build/mesh-culling-build.log`). A desktop-only native gameplay test alternated the original and relaxed mask five times in one running process. The first four pairs captured 7,976–8,038 versus 9,238–9,296 triangles, including 1,158–1,159 versus 2,060–2,064 wholly offscreen triangles. All ten samples reported zero dropped geometry; the final pair crossed a scene/camera change. Animation continued between samples, so these are runtime submission comparisons, not identical-frame visual proof. Raw samples and counts are under ignored `build/mesh-culling-comparison.json`. The relaxed mask was restored afterward. Initial headset testing exposed the wall-visibility issue below; full-level coverage and performance remain unchecked. Request authorization before future VR launches.

Reference: [hotd2-re model renderer](https://github.com/GitFist/hotd2-re/blob/8536c9141fe55602a6ed4dfe965dadc4a91fd775/src/port/render/model_renderer.cpp) helped locate the native path; the original executable's disassembly and Direct3D7 constants establish the actual patch. No rebuilt game or external assets were used.

### Wall visibility after expanded mesh capture

User reported unwanted wall surfaces covering the original forward view during the headset test. Inspection found a separate rendering gap: the bridge discarded `D3DRENDERSTATE_CULLMODE`, and the viewer always used `D3D11_CULL_NONE`. Expanded submission can therefore expose the reverse sides of additional walls. This is a confirmed missing render state and a plausible cause of the reported obstruction; the exact wall was not captured for an A/B comparison.

The bridge now packs the native cull mode into previously unused style bits 22–23. Vertex/frame sizes remain unchanged. The viewer includes those bits in draw-run grouping and selects matching hardware rasterizer states: D3D7 CW rejects D3D11's default clockwise front faces, CCW rejects back faces, and NONE retains both sides. The rasterizer returns to NONE after native runs, preserving the separate gun and floating-panel behavior. Native strip winding and baked world transforms remain intact. The mesh-expansion patch remains enabled; no character-only hook or lighting-based classification was added.

Regression checks first failed on discarded bridge cull state and on a visible triangle that should be rejected. After the fix, fake-COM capture checks cover all three native modes; offscreen WARP rendering checks cover both triangle windings under every mode through both world/sharp paths. Full build/offline checks passed (`build/native-face-culling-build.log`). On the subsequent authorized VR retest, the session reached FOCUSED and centered on tracked poses (`build/native-face-culling-vr.log`); user reported it seems to work and approved committing, while requesting more testing before full acceptance. This is provisional headset confirmation; broader scene coverage and expanded-capture performance remain pending.

The game exposes a live perspective matrix. A sampled 640x480 frame gave `fx=640.2079`, `fy=640.2079`, center `(320,240)`. XYZ vertices are transformed through the game's world and view matrices; transformed/lit vertices use reciprocal W. The bridge changes only Z's sign to supply the viewer's right/up/backward axes.

Native geometry uses repeating, including negative, texture UVs. Clamping them produced stretched stripes. Protocol v2 carries each texture's atlas rectangle separately from its original UVs; the world shader repeats UVs within that rectangle. Desktop inspection then showed textured level geometry, characters and native HUD elements. This is not complete visual qualification of lighting, transparency or HUD depth.

VC2VR's actual `gun.obj`, `gun.mtl` and `gun.bmp` are included. The viewer reports 512 gun triangles and a 1024x1024 texture. Real scale starts at an estimated 10 captured units per metre and needs headset calibration.

## Verification evidence

- `build.ps1`: x86 bridge/launcher and x64 viewer compiled; projection, FVF and exact device-prompt self-check passed.
- `tests\test_live.py --input` passed before starting the VR viewer: both players' three aim positions matched game memory; stale input stopped owning the axes; original axis-write instructions remained intact.
- Fresh read-only live check during VR initialization: frame 1438, 6,588 vertices, 617 draw calls, 239 cached textures, zero dropped triangles; finite vertex and atlas-bound checks passed.
- Desktop preview loaded the VC2 gun assets and displayed reconstructed textured gameplay. Diagnostic screenshots and logs are under ignored `build\`.
- After the user connected the headset, the active OpenXR runtime was Virtual Desktop's `virtualdesktop-openxr.json`. `viewer-vr.log` recorded successful OpenXR 1.0 instance creation, headset discovery, controller bindings, 2612x2776 eye buffers, session running and recentering.
- User confirmed the 3D scene, both gun meshes, A-button response, good aiming and trigger haptics. Original trigger implementation did not fire the native gun.
- Native-input fix: full build and fire-edge unit checks passed. A live IPC probe before reconnecting the viewer observed both native shot fields on press and repress, with no repeat edges while held. Read-only rendering check afterward: frame 1418, 7,647 vertices, 674 draw calls, 241 textures, zero dropped triangles. EXE and INI hashes remain unchanged. Logs recorded native ammo dropping from 6 to 5 and eventually 0; user then confirmed firing works in the headset.
- User reported white texture artifacts during that retest. Logs reached 525 cached textures and then repeatedly reported `texture atlas full`. The old cache retained every surface forever and returned an untextured vertex on allocation failure, which the viewer rendered using flat color. This establishes an overflow path to white surfaces; it does not identify every reported artifact.

## Texture overflow patch

The cache now allocates padded tiles in reusable 16-pixel cells. Under pressure it releases the least recently used surface not referenced by the current captured frame. Live tiles retain their positions; new tiles reuse freed space. Cache entries still own a surface reference until eviction, preventing COM-address reuse from selecting stale content. Texture failures and cumulative evictions are included in frame diagnostics.

Reused pixels and geometry publish under the same frame seqlock. The viewer stages both copies and swaps buffers only after confirming a consistent generation. Rejected reads retain the preceding geometry, atlas and metadata. This avoids showing old geometry with newly reused tiles.

`build.ps1` passed the allocator/trigger/projection tests, an isolated fake-COM bridge test spanning 1,600 surfaces, and the actual viewer snapshot reader test with interrupted copies. Eviction, current-frame pinning, COM-reference release, tile reuse and rejection of torn snapshots passed. These tests start neither the game nor OpenXR. The user initially requested no launch, then explicitly authorized another VR test. That test still showed white splash-like artifacts, despite `texture_failures=0` and no atlas exhaustion. Original artifacts and source are retained under `build/before-texture-cache/`.

Remaining limits: all textures visible within one captured frame must fit the fixed atlas and 2,048-entry cache; allocation can fail if that live working set exceeds capacity or fragments available space. Mutable cached surfaces still lack dirty tracking. Real gameplay performance and visual artifact removal remain unverified.

## Material and blending correction

The VR-only white artifacts persisted while the native game image looked normal. A stable snapshot at frame 4415 contained 138 untextured triangles, all forced to pure white by the old conversion. A native render-state trace then identified untextured XYZ draws with black diffuse material and alpha 0.5/0.7, plus textured effects with varying material alpha. Native blending used SRCALPHA/INVSRCALPHA (5/6); another observed combination was 5/5. The old bridge supplied white when XYZ vertices lacked diffuse colors, replaced every vertex alpha with a cutout threshold, and drew all world geometry with blending disabled. This explains white shadow geometry and opaque effect surfaces independently of atlas capacity.

Protocol v3 adds a per-vertex native draw style (48-byte vertices; shared mapping 83,544,220 bytes). The bridge carries diffuse material RGBA for lit XYZ input, preserves transformed vertex RGBA, and records blend factors, depth-write state, texture/diffuse alpha selection and alpha-test comparison/reference. The viewer draws consecutive runs using their native blend/depth state and preserves capture order. Both world and sharp-overlay shaders produce opacity and perform the captured alpha test. This restores material tint/opacity; it does not implement full native light evaluation.

The complete build passed: material conversion and trigger checks, 1,600-surface cache regression, interrupted snapshot copies, and offscreen D3D11 WARP pixel tests against the actual world/sharp shaders. Pixel checks cover black half-opacity shadows, fully transparent primitives, texture/material alpha modulation, texture-only alpha, alpha-test rejection and both observed blend-factor pairs. The new VR session connected successfully; user confirmed the result looks better in the headset. This confirms improvement, not complete elimination of every artifact. Diagnostic snapshots/logs and source backup are under ignored `build/` (`before-material-blending/`).

## Pending acceptance

User confirmed scene/gun visibility, aiming, firing, immersive reload and improved appearance after material/blending correction. Atlas changes alone did not eliminate the reported white artifacts. Revised reload thresholds, correct head tracking, stereo comfort, scale, HUD depth, player 2 joining, long-session texture-cache behavior and full-game coverage still need acceptance. Do not call the conversion finished before those checks.

## Native mode-selection menu alpha

The clipped/black Arcade/Original/Training selection screen reproduces with the native windowed game and no bridge. Title artwork renders fully; keeping the mode menu focused does not resolve it. A native draw probe found successful draw calls and intact RGB artwork, but all nine sampled 32-bit menu textures had alpha zero. The game enables alpha testing with `GREATER_EQUAL`, reference 1, so those textures are discarded. Disabling alpha testing for those diagnostic draws restored the complete menu; the user confirmed it worked. This is independent of VR HUD distance.

The native bitmap loaders at `0x49E9F0` and `0x49F100` use GDI `BitBlt` into DirectDraw surfaces. GDI copies opaque RGB artwork but leaves the ARGB destination alpha zero. The bridge now hooks surface `ReleaseDC` and repairs alpha only for returns to `0x49EAAB` and `0x49F1A9`, after the upload succeeds, and only for RGB/alpha surfaces with 32-bit pixels and an `0xFF000000` alpha mask. RGB remains unchanged; native alpha testing remains enabled. Other callers, 16-bit textures and surfaces without alpha are untouched. Both paths share the same repair.

An asset-header scan found 47 BMP files under `tex`: 44 RGB24 and 3 indexed8, including game-mode labels, options, rankings, network/exit graphics, title images and publisher logos. These are opaque bitmap sources; the repair covers their affected uploads without texture-name rules. Original files remain unchanged. Offline checks cover caller selection, opaque alpha restoration with RGB preservation, skipped formats and failed locks. A fresh bridge-only desktop session restored the full mode menu without diagnostic DLLs (`build/menu-source-fix.png`); `build/HOTD2Bridge.log` records repaired uploads. Other screens and headset presentation still need visual acceptance. `--native-window` intentionally remains a baseline without the bridge repair.

## Startup head-pose anchoring

The user reported needing an initial manual recenter, as previously observed in Battle Pod. Current Battle Pod native-VR code (`C:\src\gaming\starwars-battlepod\.worktrees\native-vr\plugin\native-vr\TrackingMath.hpp`) requires FOCUSED plus position/orientation VALID and TRACKED before initial anchoring; its runtime notes describe VDXR changing origin between READY and FOCUSED. Both HOTD2 and the inspected VC2VR source previously anchored on the first renderable pair of views, ignoring locate failure and pose validity/focus.

HOTD2 now checks successful `xrLocateViews`, exactly two views and valid position/orientation before rendering. Without an anchor (including a pending grip recenter), it additionally requires FOCUSED and both TRACKED flags. Waiting frames submit zero layers and cannot commit an origin. After centering, normal focus changes do not invalidate or replace it; valid inferred poses remain usable. The existing grip action still requests a new center on the next qualified frame. Logs include session transitions and centering flags/position. No timer or repeated automatic recenter was introduced; donor source remains unchanged.

Offline regression checks reject READY/VISIBLE bootstrap samples, each missing validity/tracking flag, locate failure, incomplete stereo views and invalid later poses; they accept focused tracked startup and preserve established anchoring across loss of focus. Full build and checks passed (`build/startup-pose-build.log`). The subsequent headset test logged initial centering at session 5 (FOCUSED), flags `0xF`; the user confirmed startup centering works. The first launch exited unexpectedly before the game published a frame; its cause remains unknown. A fresh desktop game remained running, then the attached VR viewer remained running and centered successfully. That retry is the confirmed headset result.

## Mode-menu thumbstick navigation

The original VC2VR controller bindings used vertical stick motion for manual zoom and stick click to cycle the floating screen. These changed presentation instead of moving the native game-mode selection. Both controller profiles with sticks now bind vertical input to menu navigation; Vive trackpad vertical uses the same action. Stick click has no screen binding. Automatic menu-screen display remains; desktop M/Z diagnostic keys remain.

Each hand engages at 0.65 deflection and releases inside 0.30; inactive/nonfinite input clears it. UP/DOWN travel as input bits with unchanged v3 layout. The bridge merges both hands, cancels opposing directions and injects native DirectInput arrow keys only during mode-selection phase 4. Native held-key repeat owns repetition. Gameplay receives no VR arrow injection. Existing Start/Back/grip and independent firing/reload remain. Tests cover stick deadzone/hysteresis, disconnect/nonfinite input, each hand's arrow route, opposing input, gameplay gating and watchdog release. Build/offline checks passed; headset navigation still needs acceptance. The user confirmed the preceding alpha repair restores the mode menu in VR.

## Immersive reload

User reported A pauses and B skips cutscenes, so face buttons are not reload bindings. After confirming reload works at 60 degrees in the headset, the user requested 50 degrees up and 45 degrees down. A fresh trigger pull now measures the gun in current headset-local axes: reload at pitch up 50 degrees, down 45 degrees, or yaw 60 degrees to either side. The earlier implementation used tracking-space vertical and ignored headset pitch; it did not use the scripted game camera. Both guns now remove current tracked head orientation before classification, independently of the recenter origin and native projection. These are VR design choices, not Flycast thresholds. Each controller latches shot/reload until release; moving a held trigger between angles cannot change actions. Tracking loss disarms the current pull. Akimbo maintains separate hand states and commands.

`VC2_BTN_RELOAD` is a new input bit; shared frame layout stays v3. During gameplay (native phase 6, active player status 5), mouse/gun control receives an invalid-aim trigger edge, which the native weapon code interprets as reload. Keyboard/joystick control receives the selected native control scheme's reload-button press mask. Both feed native reload handling; no ammo values are written by the bridge. Menu/inactive-player reload requests suppress firing and do not inject reload. A sends Enter, X sends the player's native Tab start key; neither sends Ctrl. Gun reload animation now follows the reload command, not A/X.

The first headset reload test reached the bridge but every request logged `queued=0`: the guard incorrectly required status 4 (continue handler `0x414280`). Native player initialization at `0x41491A` selects status 5 from `0x579DEC` and installs active handler `0x413E90`, which calls weapon handler `0x414B90`. Corrected the guard and regression fixture to status 5; reload logs now include phase, status and control type. Failed-run logs were preserved in `build/reload-failed/`.

After correction, all offline checks passed and a desktop-only smoke test logged `queued=1 phase=6 status=5 control=1`. That session remained in attract playback (`0x9C8E00=0`), so shots did not consume ammo and refill could not be validated. Test game closed normally; VR was not launched. Ask the user before future VR launches.

Offline tests cover both sides of the distinct up/down thresholds, horizontal shots, action locking, independent hands, lost tracking, nonfinite directions, native gun and keyboard reload paths, preserving physical button bits, one press edge per pull, menu/player guards, watchdog expiry and separate pause keys. User confirmed ammo reload after the status-5 correction. Revised angle comfort, pause behavior and both-player operation still need runtime acceptance.

## Reload/Cancel face buttons

After checking PC native input, B/Y now send each player's Reload/Cancel key (`DIK_RCONTROL`/`DIK_LCONTROL`) instead of Escape. Native weapon handlers at `0x414B13`/`0x414E29` set the cutscene skip request at `0x9A1A18` from Cancel input while gameplay is suspended; event handler `0x435F40` consumes it and advances the event cursor. The prior README claim that Escape itself was the skip action was not established by that code.

Each hand's action is sampled separately: B belongs to right/P1 and Y to left/P2 in both modes after the independent-player correction below. Cancel also queues a native gun reload on its press edge, covering the selected mouse/gun profile as well as keyboard control. This uses the same phase/player guards and native reload path as the angle gesture. Cancel edges are independent of trigger edges, so holding B/Y does not suppress a later shot. Tests reproduce and cover the mapping, player separation, one reload edge per press, no injected Escape, held-Cancel firing and existing menu/watchdog guards. Runtime cutscene skip and headset button routing remain unverified.

## Akimbo and independent player joining

Existing `[vr] akimbo` keeps VC2VR's name and the supplied default 1. Right always remains P1 and left P2. With akimbo on, both tracked gun models are visible before joining, and the bridge merges either player's requested Start into both native DirectInput keys (Enter/Tab). Physical keys and independent aim/fire/reload remain intact. The native game joins both players from the same menu confirmation, so no deferred joins or game-state writes were added.

The first headset test with akimbo off exposed a misunderstood requirement: the other hand took over P1. Corrected behavior lets each hand join its own player independently. The initial correction latched visibility on each hand's Start. User then identified that Start alone does not establish a successful native join. Visibility now follows native status, with right/P1 and left/P2 fixed. Tracking loss hides only the affected gun, and hidden hands retain the trigger-disarm-until-release rule. Native pause/confirm remains attached to each player's Start.

Protocol v6 originally retained the v5 mapping size and offsets; v7 below adds native player status. The legacy shared `akimbo` field enables the P2 input block in both VR modes; a separate `VC2_BTN_PAIR_START` request bit enables Start mirroring only when the user selected akimbo. This separates native P2 routing from automatic paired joining. The viewer draws P2 whenever its own gun is visible, independently of the setting. Mocked OpenXR checks exercise both first-hand choices, both guns remaining visible after separate joins, independent trigger/Start routing, tracking loss/recovery and paired startup. Native keyboard tests cover paired and independent keys. Full build/offline checks passed (`build/independent-players-build.log`).

Initial paired desktop check (`build/akimbo-join-live.json`, owned PID 9124): one P2 Start joined both native players with states `[5,5]` and lives `[3,3]`. After correcting independent mode, two fresh desktop sessions verified P1-first `[5,9] -> [5,5]` (PID 3172) and P2-first `[9,5] -> [5,5]` (PID 68564). Both remained unpaused with three lives each. Reports: `build/independent-join-p1-first.json` / `build/independent-join-p2-first.json`. All three cheats were requested at 3 shots/sec; owned test games closed normally. Corrected independent-player headset presentation, paired pause/continue and P2 gameplay still need acceptance. No VR launch was performed for this correction.

Native status correction: P1 is the signed WORD at `0x9A5C62`, P2 at `+0x130`, and game phase is the DWORD at `0x9C8E98`. The native state table at `0x579CD0` and player handlers identify join/rejoin transitions 0-3, active state 5 and accepted-waiting state 10. Only those states in phase 6 enable independent gun visibility. Continue 4, out/game-over 6-7, name entry 8, idle 9, invalid states, attract mode and menus do not. Reads only; no native player state writes. Akimbo retains both startup guns.

Protocol v7 appends `active_players` (P1 bit 0, P2 bit 1) at byte 83,544,232, making the mapping 83,544,236 bytes. The bridge publishes it under the frame seqlock; the viewer accepts it only with a coherent snapshot. This replaces the Start latch, covers physical-keyboard joins, and retains hidden-trigger release guards. Offline tests cover unaccepted Start presses, both native player mappings, waiting/continue/attract status, status loss, paired startup and interrupted snapshot reads. Full build passed (`build/native-player-status-build.log`). Headset presentation remains unverified after this change.

Desktop runtime check (owned PID 11368, `build/native-status-p1-first.json`) verified published masks against native states: menu `[9,9]` -> mask 0, P1 `[5,9]` -> mask 1, both `[5,5]` -> mask 3. Both had three lives and remained unpaused. All cheats requested at 3 shots/sec. The test game closed normally; no VR launch.

## Optional lives, ammo and rapid-fire cheats

VC2VR's viewer uses `[vr] infinite_lives` / `infinite_ammo` and `--infinite-lives` / `--infinite-ammo`. HOTD2 now accepts the same names plus `[vr] rapid_fire` / `--rapid-fire`. `Start-HOTD2.ps1` forwards `-InfiniteLives`, `-InfiniteAmmo`, `-RapidFire`. All three default to 0; command-line flags only enable the corresponding feature for that viewer session. Active config keys were added at 0 with the prior file saved as `build/HOTD2VR.ini.before-cheats`.

The original HOTD2 executable already checks native cheat bytes: lives at `0x9C9FD8 + player*0x7C`, ammo at `0x9C9FD9 + player*0x7C`. Both damage paths (`0x415342`, `0x415464`) skip life loss and its score/rank penalty when the lives flag is set. Normal and Original weapon paths check profile offset `0x79` (`0x4149D9`, `0x414C47`) before consuming ammo. These four instruction signatures are validated before flag ownership is allowed. The bridge now saves each player's prior flag bytes on first fresh viewer ownership, forces each supported flag to 0 or 1 according to the viewer setting, and restores saved bytes only on disconnect/heartbeat expiry. Earlier enable-only ownership preserved pre-enabled native cheats when an option was 0; that behavior was corrected. Native free play follows the same override rule. Counters and the original game INI are not edited. This native behavior does not refill an already empty gun or resurrect a dead player.

Protocol v7 retains the independent `cheat_flags`, `cheat_tick` and `rapid_fire_rate` footer introduced in v5 (existing vertex/header and cheat offsets unchanged). Every viewer mode refreshes the request from the frame-reading loop without acquiring gun input. A 500 ms watchdog restores native flags if requests stop. Normal viewer shutdown clears requests. Rapid fire additionally requires fresh gun input, phase 6, active player status 5, unpaused play and native `canShoot` at `0x9C8E00`. Each player's timer obeys the requested rate; it does not catch up in bursts. Release, reload, tracking cancellation, inactive players and stale input reset repetition. Existing native weapon logic consumes the shots. Visual recoil still follows trigger press edges. Haptics now follow confirmed native shots with a separate 10-pulses/sec cap; see the shot-feedback section below.

Offline checks cover native signature acceptance/rejection, all flags defaulting off, each command-line flag, temporary-file INI parsing, independent cheat heartbeat delivery, preservation/restoration of nonzero native flags, unsupported layouts, separate hand timing, timer wrap, release/repress, menu/pause/cutscene/player guards, reload priority and input/cheat watchdogs. Full build passed (`build/cheats-build.log`). Launcher parameters parse without starting the game.

Initial desktop-only runtime test at 10 shots/sec (protocol v4): game PID 27132, phase 6, active P1 with native shooting enabled. Both players' native flags changed from `(0,0)` to `(1,1)` and restored to `(0,0)`. Held-trigger rapid fire increased P1 shots from 0 to 11 over approximately 1.2 seconds while ammo stayed 6. Disabling only infinite ammo while holding fire reduced ammo to 0. A separate `--window --seconds 3 --infinite-ammo --infinite-lives --rapid-fire` viewer session logged all flags enabled, activated native cheats and restored them on exit. Evidence: `build/cheats-live.json`, `build/cheats-cli-desktop.log`, bridge log. The owned desktop test game closed normally. The subsequent authorized VR test enabled all three cheats (`build/cheats-vr.log`); user reported rapid fire felt much too fast. Actual damage protection, P2 in-game firing and Original-mode weapon coverage remain unverified.

User compared rapid-fire rates in authorized headset tests: 10 felt too fast, 2 too slow, then 4 and 3 were tried. User accepted **3 shots/sec** as the default after the final test (`build/rapid-rate-3-vr.log`: focused session, tracked centering, all three cheats requested). `[vr] rapid_fire_rate=3`, `--rapid-fire-rate N`, and launcher `-RapidFireRate N` accept integers 1–30; rate alone never enables rapid fire. CLI overrides INI for one launch. Invalid INI values log a fallback to 3; invalid/missing CLI values return exit code 2 before starting OpenXR. The bridge independently rejects invalid rates. Native frame quantization can lower the achieved rate. Boundary/timer-wrap checks and fake native input verify the rate reaches the hook and changes its interval; final build/checks are logged in `build/cheats-final-build.log`. Active INI backups: `build/HOTD2VR.ini.before-cheats`, `build/HOTD2VR.ini.before-rapid-rate`, `build/HOTD2VR.ini.before-rate-4` and `build/HOTD2VR.ini.before-rate-3`. The original game EXE/INI hashes remained unchanged after desktop testing.

The earlier 30 shots/sec figure is a code-derived input ceiling for the ordinary pistol, not a measured human cadence: native init calls `HGL_SetFrameRate(60)` at `0x49E55C`; mouse input at `0x41F370` uses sampled press edges, requiring a release sample between shots; the normal weapon handler adds no shot cooldown. The current default 3 is the user's comfort choice. User requested all future playtests enable infinite lives, infinite ammo and rapid fire; ask before launching VR.

## Quit confirmation bypass

The exact quit question now returns Yes through the existing `MessageBoxA` hook, with no extra config or on-disk game patch. Scope is injected desktop/VR sessions. Unit checks against the actual hook first reproduced the missing auto-accept, then passed after implementation; unrelated questions, wrong title/button flags and null inputs still reach the original API. Full build and offline checks passed.

Two native `WM_CLOSE` tests auto-accepted the quit question and the game exited normally within five seconds, verified by process handle and bridge log. A separate IPC `VC2_BTN_BACK` test during startup did not exit within five seconds; Back was not changed into an unconditional quit command. That test process was subsequently closed through the verified window-close path. User then tested VR quitting and confirmed it works; the bridge logged auto-acceptance and both game/viewer processes exited. Logs are under `build/quit-desktop-test.log` and `build/quit-vr-input-test.log`.

Do not run the input self-check while someone is using the viewer. Do not kill unrelated VR services or other game processes. Old or uninjected sessions can still show the quit confirmation; terminate only the explicitly identified HOTD2 test process when a rebuild requires it.


## VR shots confined to the original viewport

User clarified that the spark follows aim within the original flat-screen area rather than remaining at one point. The earlier native desktop diagnostic (`build/shot-aim-diagnostic.json`) tested only in-bounds coordinates, so it did not rule this out. Two clamps explained coordinate confinement: `aimFromPose` limited world projection to x 0-639/y 0-479, and bridge `update_input` independently limited it to x 0-640/y 0-480. The native weapon copies signed gun axes into the player's aim and uses them for shot rays and view-space flashes, so those limits also bound native effects.

Removed both viewport clamps from world aiming. The bridge still rejects nonfinite coordinates and saturates to the signed 16-bit axis range before rounding; controller-trigger angle reload remains explicit. Floating-screen/menu projection retains its existing bounds. No native state, collision or particle-rendering routines were patched. Regression `tests/test_reload.c:47` failed before the change with an offscreen target; full build then passed (`build/wide-aim-before.log`, `build/wide-aim-build.log`). Actual viewer tests cover projection beyond all four edges and unchanged menu-center mapping; native hook tests cover both players, valid shots beyond old bounds, finite saturation and nonfinite rejection. Headset spark placement and native effects clipped elsewhere by the game remain unverified. Crosshair suppression is a separate unresolved issue.

Desktop native-input proof after removing the clamps: owned PID 75968 (`build/wide-shot-aim-diagnostic.json`) accepted P1 target `(-100,-80)` as gun/player axes `(-420,320)` and P2 `(740,560)` as `(420,-320)`. Both players spawned flashes using those expanded aim values divided by native focal length 640.20795, rather than screen-edge values. This verifies native coordinate propagation and flash generation, not headset rendering or enemy hit registration outside the original frustum. All cheats requested at 3 shots/sec. Test game closed normally; no VR launched.


## Close-target aim depth and native shot flashes

Offline reproduction identified a donor mismatch: HOTD2 capture wrote painter `depth=0` for every vertex, while inherited VC2 `rayVsScene` rejected depth 0 as sky. World aim therefore always used `aimreach` (default 6 m). A controller at `(0.2,0,-0.4)` aiming at a plane 0.6 m from the native camera produced x=335.625 instead of x=486.667. The old `t>1` filter also discarded targets within one game unit (10 cm at the current scale). Removing 640×480 coordinate clamps alone did not fix either depth problem.

Protocol v8 preserves v7's vertex/header/cheat/status offsets and appends 64 exact camera-space flash anchors (three floats each), making the mapping 83,545,004 bytes. It adds capture provenance in unused style bits: `HOTD_WORLD` bit 24, `HOTD_SHOT` bit 25, plus a six-bit contiguous flash group. Native model draw hooks at `0x4A7EF0` and `0x4A8440` scope world capture around actual submissions; native screen sprites outside those model calls do not become aim targets. Both hooks validate exact complete-instruction prologues before detouring. Shader blend/alpha/cull masks ignore the metadata. Painter depth and native UI filtering remain unchanged.

Flash recognition uses HOTD2's own model bases at `0x579F78` (373, 383, 2934, 2948), nine animation models per bank, resolved through live model slots at `0x9A66A4 + id*16`. Only matching models on the native camera plane at |z|=1 are marked as shot effects. Traveling tracers reuse some models at genuine world depths; they retain those positions but are excluded from aim and splash intersection, avoiding feedback from the previous shot. Capture flags are restored after each model call, including nested submissions.

The viewer now intersects tagged world triangles regardless of painter depth and accepts hits more than 1 mm in front of the ray origin. It honors native CW/CCW culling so invisible backsides cannot block aiming or splash placement. Near world points also project with a 1 mm camera-depth guard instead of the old one-game-unit cutoff. Native gun placement and game hitboxes are not altered.

After a coherent frame copy, each native flash group is raycast from the original camera through its exact native transform origin onto the current world's geometry. Its vertices reproject onto a camera-facing billboard 2 mm in front of that surface, preserving the original angular shape. Native shot meshes have depth variation; flattening their layers prevents that variation from expanding into a thick volume or cutting through the wall. Different hands stay independent. Animation layers retain the same native origin, so an asymmetric mesh cannot move one layer onto another surface. Anchors and vertices are accepted together under the frame seqlock. A miss uses `aimreach` rather than a HUD plane. Actual world impacts/blood effects are not reclassified as native camera-plane flashes.

Full build/offline checks passed (`build/near-aim-splash-final-build.log`). Tests cover zero-painter-depth world targets, rejecting HUD/flash targets, 5 cm controller/camera distances, both off-center flash groups, fallback placement, preserving world geometry, camera-plane model identification versus traveling tracers, scoped capture flags, conversion metadata, asymmetric layers sharing a shot origin, flattening mesh thickness without changing projection, and coherent anchor snapshots. WARP pixel tests additionally compare raycast face rejection with actual D3D11 CW/CCW rendering. Headset results, special Original-mode weapon effects and native off-frustum hit registration remain unverified. Crosshair suppression remains separate. No VR launch.

Final desktop native check (owned PID 17500, `build/near-splash-native.json`) captured coherent frames for both players with 31,680 / 31,848 world vertices and 126 independently tagged flash vertices per shot. Both players joined, remained unpaused and had three lives; all cheats were requested at 3 shots/sec. Offline replay of those real captures through the actual `placeShotEffects` function confirmed both layers of each flash share one surface depth, with projection error below 3e-8 in normalized camera coordinates. Surface distances were 154.996 and 32.7182 game units; billboard camera depths were 144.990 and 30.5913 units, respectively. The owned game closed normally. This proves capture/classification/placement math, not headset appearance or full in-game hitbox acceptance.


Headset acceptance after these changes: user reported "works great" in the authorized VR session (`build/near-depth-splash-vr.log`, game PID 46980, viewer PID 57920, focused OpenXR with tracked centering). Active test INI used `akimbo=0`; CLI enabled all cheats at 3 shots/sec. This confirms user acceptance of the tested close aiming/splash result. Full-game coverage, special Original-mode effects and crosshair suppression remain separate.


## Rapid-fire default revised to 4 shots/sec

Default is now 4 shots/sec in the shipped INI, viewer initialization, missing/invalid INI fallback and PowerShell launcher parameter. Active local INI was updated as well; backup is `build/HOTD2VR.ini.before-default-rate-4`. Explicit rate overrides and the supported 1-30 range are unchanged. Earlier rate-3 acceptance and test reports above remain historical evidence.

Build/offline gates passed after the default change (`build/default-rate-4-build.log`), including missing-key default, shared request value and the existing 250 ms rate-4 timer check. No game or VR launch.


## In-game HUD at main-menu depth

The bridge previously discarded some flat 2D draws and captured other HUD sprites at their native near-camera depth. Live tracing found biased HUD planes at depths 0.98 and 0.9999 native units (rhw 1.02041 and 1.0001), explaining the elements sitting close to the face. Native screen-space sprite and bitmap draws outside model capture now collect into a bounded HUD buffer in every mode and at every native sort depth. They append after world geometry under the existing frame seqlock; `reserved[2]` reports the first HUD vertex plus one. Model capture is tracked separately from collision metadata: traveling tracers deliberately have no world/shot tag but must retain their actual depth. The automatic screen-panel presentation replaces the separate HUD layer when its texture is available, preserving bitmap menus without duplicating UI. Scope rendering keeps HUD because the scope does not draw that panel.

HUD canvas and menu pose share constants in `capture_math.h`: 2 m distance, 1 m half-width and 0.75 m half-height. Native 640×480 HUD coordinates map directly onto that plane using the active units-per-metre value, so recentering and world scale place it with the menu. Native vertex/texture transparency and blend/alpha-test states are retained. HUD vertices have no world/shot/group flags, no face culling and no depth writes. The existing HUD render pass disables scene depth testing, keeps its unzoomed matrix and uses the sharp overlay shader. Captions/lives/ammo cannot become aim or splash targets. Auto-panel visibility now tests the world vertex boundary, so HUD text counts cannot suppress the menu panel.

Protocol v9 changes layer semantics without changing v8's 83,545,004-byte layout; matching bridge/viewer builds are required. Test reproduced the old discarded-HUD behavior (`frame->vertex_count==6` failed before implementation). Full build passed (`build/hud-menu-depth-build.log`). Actual bridge tests cover world/HUD publication order, all canvas corners at 2 m, transparency, cleared collision flags, main-menu exclusion and depth-disabled HUD capture. Viewer snapshot tests cover the HUD boundary, and WARP verifies the HUD remains visible over nearer scene depth. HUD size/depth comfort, full caption coverage and headset presentation remain unverified. No VR launch.

Final desktop check (owned PID 108792, `build/hud-native.json`) captured coherent frames for both active players with 156 / 132 HUD vertices, all at native z=-20 (2 m at units_per_metre=10). No unclassified near-camera vertices remained outside the HUD layer in either capture. World geometry and 126 shot-effect vertices remained separate. Both players had three lives and were unpaused; all cheats were requested at 4 shots/sec. Owned game closed normally. This checks capture, depth and separation; headset size/readability and full-game UI coverage still require user testing.

User accepted HUD placement and later confirmed HOLD YOUR FIRE looks correct; dialogue subtitles need another headset check. Native intro capture and actual viewer shader replay (`build/caption-reference.png`, `build/caption-replay.png`) both show "We're meeting G over there." The 138 captured subtitle vertices are already in the 2 m HUD layer; no separate subtitle depth change was made.

Letterbox_Task 0x00413970 draws two instances of model 0x93E with identity view-space bases, y translations +/-0.35 through +/-0.45 and z=-1. Capture now omits only those exact bar instances in every game mode. The original model draw still runs for the native desktop window. Pause dimming, full-screen blackouts and other uses of the same model remain captured. Fake-COM regression verifies animated top/bottom bars, unchanged native draw calls, scope restoration, pause/blackout preservation, other models and cross-mode bar removal. Bridge rebuilt and checks passed; headset verification pending (`build/caption-letterbox-vr.log`).


Title/attract logo regression: the initial phase-6-only HUD filter discarded the title logo (depth 1) and PRESS START (depth 0.9999). Their 3D background keeps the automatic captured-screen panel hidden, so neither fallback displayed them. The first correction admitted 3 and 5 alongside 6; the broader correction below removes that filter entirely. Added actual-bridge capture/publication checks for both native depths in title and attract over world geometry. Test failed before the fix at the world/HUD vertex-count assertion and passed afterward. Bridge rebuilt; no game or VR launch for this change.


All-mode UI correction: the mode whitelist still discarded how-to-play (9), story (10/11), game-over (7) and menu text outside admitted modes. The depth cutoff also misclassified ranking/map/options artwork, whose native sort depths reach 200. Both filters are removed; original native submission order remains the HUD paint order. Actual-bridge tests exercise modes 0..16 at depths 0.9, 0.98, 0.9999, 1, 2, 21, 45, 101, 120 and 200, with world/HUD separation and 2 m placement. They reproduce the old missing-UI assertion before the fix. Additional checks protect pretransformed world/shot geometry and untagged traveling tracers. WARP verifies screen-panel replacement, scope HUD and fallback when the panel texture is unavailable. Full build/offline checks passed (`build/all-mode-ui-build.log`). No VR launch; full-game visual coverage remains pending.


Desktop attract-loop verification (owned game PID 101744, viewer 87540) passed with cheats enabled at 4 shots/sec. Coherent live frames in title (3), tutorial (9), and demo (5) contained up to 54, 72, and 120 HUD vertices respectively, all at native z=-20 with world/shot flags cleared. Tutorial retained 13,974 world vertices plus 66 UI vertices in the replay fixture. Actual viewer shader replay (`build/tutorial-replay.png`) visibly retains PRESS START, six bullets, the P1 label and three life torches seen in the native reference (`build/tutorial-reference.png`). Report: `build/all-mode-ui-native.json`; build: `build/all-mode-ui-build.log`. Both owned diagnostic processes were closed normally. Other modes have synthetic capture coverage, not complete native visual acceptance; headset testing remains pending.


## Per-material texture addressing

User reported a symmetry seam / wrongly textured half on the opening car and a boss, visible in the conversion desktop window as well as VR while the native game looks correct. The bridge read blend/alpha/cull state but discarded D3DTSS_ADDRESSU/V; both conversion shaders applied frac(UV) unconditionally. Native material state supports wrap, mirror and clamp independently per axis. VC2VR handles its own engine's quad flips in its UV builder, so its behavior is not a substitute for capturing HOTD2's D3D7 sampler state.

Bridge now captures both address modes per draw, encodes zero-based U/V modes in a new uint32 address vertex field, and leaves raw UVs intact. Both world and sharp HUD shaders address interpolated UVs inside the selected atlas tile: wrap via frac, mirror via a period-two triangle wave, clamp via saturate. Atlas gutters still isolate neighboring tiles. Draw batching keeps its existing blend/cull grouping because address metadata reaches the pixel shader per primitive. Generated laser/gun vertices initialize the new field. Protocol v10: 52-byte vertices; total mapping 84,505,004 bytes; cheat fields at 84,504,220, shot anchors at 84,504,236. Matching bridge/viewer builds are required; old v9 captures remain historical fixtures.

Actual bridge fake-COM checks verify all nine U/V combinations and snapshot publication retains addressing metadata. Actual WARP pixel checks cover both shaders and axes at negative/out-of-range UVs, positive/negative integer seams and in-range samples. Full build/offline gates passed (`build/texture-address-build.log`).

Native opening-car inspection (owned desktop PID 110596, `build/car-address-native.json`) confirmed model 0x1579 has U-mirror/V-clamp, U-mirror/V-wrap, mirror/mirror and clamp/clamp materials; 0x157E also uses U-mirror/V-wrap. Captured packet has 22,269 world and 156 HUD vertices with eight address-mode combinations. Replaying that same packet with captured addressing versus forced wrap shows the windshield/reflection seam changes (`build/car-address-after.png`, `build/car-address-before.png`), with native reference `build/car-address-reference.png`. Cheats were enabled at 4 shots/sec. Owned native process closed normally. No VR launch. Boss materials and final headset appearance remain unverified.


Headset verification: user confirmed textures fixed in the session logged by `build/texture-address-vr.log` (game PID 98820, viewer 92896). No claim of exhaustive boss/scene coverage.


Attract intro/outro letterbox follow-up: the bar-removal helper still required game phase 6 even after UI capture became mode-independent. Attract demo/story phases 5/10/11 use the same Letterbox_Task and model/matrix shape. Removed that phase restriction; exact model 0x93E, identity basis, y +/-0.35..0.45 and |z|=1 remain mandatory. Fake-COM checks cover 102 bar cases (phases 0..16, y +/-0.35, +/-0.40 and +/-0.45), unchanged native draw calls, restored capture scope, and preserved pause/blackout/other-model behavior. Native screen-panel fallback still contains original desktop pixels; no framebuffer cropping or native draw suppression was added. Headset attract-loop acceptance pending.


## Menu navigation across native interfaces

The bridge gated arrow injection to mode-select phase 4 even though the viewer already sent vertical stick navigation. Arrow injection now follows fresh VR input in every mode; the game consumes arrows in its own interface handlers. Added horizontal float actions and Touch/Index/Vive bindings, using LEFT=2048 / RIGHT=4096 and existing hysteresis. Either hand can navigate before a player joins. Conflicting directions cancel VR navigation per axis. Native A/X Enter/Tab, B/Y cancel and gun aiming/firing remain separate. Options difficulty/lives/etc use Left/Right to adjust values; Enter confirms only native actions such as Default, calibration/controller pages and Exit. Sound/voice Play requires native Fire, so Start intentionally is not remapped to shoot. In akimbo-off menus, the existing hidden-gun trigger guard can still limit that Play action.

Focused bridge/controller checks pass: modes 0..16, both hands, four directions, hysteresis/release/inactive inputs, opposing directions, unchanged Start keys, no extra fire and preserved independent gameplay aim axes. No VR launch; native menu/headset acceptance remains pending.

## Lighting/shader audit

User reports the conversion view appears brighter/flatter than native, initially describing missing shadows then shaders. Native lit submissions are present in actual logs (FVF 0x112 with normals, LIGHTING=1, AMBIENT=0xFFB2B2B2), but capture drops normals and specular and copies only material diffuse. Conversion shaders multiply texture RGB by that color; they do not evaluate native enabled lights, material ambient/emissive, specular or fog. The native reference implements directional, point/spot Gouraud lighting, secondary specular after texturing, and fog. This is a confirmed fidelity gap and a likely source of different shading; scene brightness has not been quantified.

Current VR logs already show sRGB eye buffers and gamma=220%, so no blanket gamma change was made. Black alpha shadow geometry is captured and has offscreen shader coverage; that does not prove all scene shadows are present. A second potential mismatch is linear-space sRGB-eye blending versus the original's encoded-color blending, especially on translucent shadows; current UNORM/gamma=1 pixel checks cannot qualify that difference. No lighting repair implemented. Next proper step is native per-vertex diffuse/ambient evaluation from enabled lights/materials/normal transforms, then separate secondary-color/fog support; specular cannot correctly be folded into texture tint.


## Native lighting conversion

The bridge now evaluates native D3D7 Gouraud lighting per vertex using current WORLD/VIEW, inverse-transpose normals, material source selection, emissive/global/light ambient, enabled light slots 0..15, directional/point/spot attenuation and cones, and local-viewer specular. Normal normalization follows NORMALIZENORMALS instead of being forced. These states are captured per draw because native actor/stage submissions change them. Pretransformed UI retains its already-lit colors.

Specular is transported separately and added after texture modulation. Fog follows that saturated sum; native linear table fog uses the original camera depth and remains unclamped until pixel evaluation. EXP/EXP2, vertex/range fog and supplied prelit fog factors are supported. Current protocol v12 retains the 64-byte vertices and 87,385,004-byte frame introduced in v11; both producer and viewer must be rebuilt together. Generated gun/aim geometry explicitly disables native specular/fog. Gamma/blending settings remain as before.

Validation: CPU lighting fixtures cover material sources, light types/range/attenuation, normal transforms, view transforms, local viewer and fog modes; actual fake-COM bridge coverage includes sparse enabled slot 15, material-vs-vertex color and fog/specular transport. WARP pixel checks cover secondary color after texture modulation, clamping before fog, linear/EXP/EXP2 fog, per-pixel table-fog clamping, and both world/sharp HUD shaders. Existing input/HUD/opacity/culling/addressing tests pass.

Native windowed car-dialogue capture (owned PID 46620, closed normally) published 15,474 world vertices, 921 with specular, 15,330 with fog, and 201 distinct diffuse colors. `build/lighting-comparison.png` places native capture left and conversion replay right. Shading/highlights now appear much closer; filtering, HUD placement and letterboxing differ, so this is not a pixel parity measurement. Full-game stage lights, bosses and headset appearance remain unverified. Existing sRGB-eye blending differences on translucent shadows are outside this change.

Formula references: [Microsoft lighting](https://learn.microsoft.com/en-us/windows/win32/direct3d9/mathematics-of-lighting), [pixel fog](https://learn.microsoft.com/en-us/windows/win32/direct3d9/pixel-fog), [vertex fog](https://learn.microsoft.com/en-us/windows/win32/direct3d9/vertex-fog), and the local native-reference renderer `C:/src/gaming/hotd2-re/src/port/render/hlsl_shaders.h`.


## Native lighting switch

`[vr] native_lighting=1` defaults enabled; viewer CLI `--native-lighting` / `--no-native-lighting` override for one launch (last wins), and Start-HOTD2.ps1 forwards `-NativeLighting` / `-NoNativeLighting`. The viewer publishes HOTD_RENDER_UNLIT through the previously unused screen-padding word (offset 96), now named render_flags; current protocol v12 retains the v11 size and other offsets. Bridge initialization reads the same INI so native-only capture also follows it. Changing the viewer setting updates the live bridge without modifying the native game's draw path.

Off restores pre-lighting diffuse/material color and neutral specular/fog. It skips extended state/light queries, normal-matrix preparation and lighting evaluation, while keeping the fixed 64-byte vertex format. INI/CLI precedence, shared flag delivery and exact bridge off/on colors are covered by offline tests; no frame-time savings have been measured. Menu UI deferred by user request.

VC2VR comparison: HOTD2 already has MSAA, supersampling, sRGB gamma handling, native depth, sharp HUD, zoom/scope and world/gun scale. Precise native-crosshair suppression and optional HUD size/distance controls are useful future work. VC2 HGL cube passes/object-memory/background stretching/dashboard or custom drum UI are not general graphics improvements to transplant here. Separate brightness gain, described below, provides comfort tuning rather than reconstructing native lighting.

## Scene brightness

`[vr] brightness=100` is neutral; integer values 25..400 apply gain to the converted scene after native specular/fog and before existing gamma compensation. It works with native lighting enabled or disabled. Gun models also apply brightness and gamma after material shading. HUD vertices (`depth>=1`), generated aim graphics, the captured-screen panel and native desktop rendering are unchanged. Gamma remains sRGB compensation, separate from brightness.

`HOTD2VR.exe --brightness 80` or `Start-HOTD2.ps1 -Brightness 80` selects 80% brightness for one launch without saving the INI. Default brightness is 90%. Missing/invalid INI values use 90; invalid CLI values are rejected and repeated valid CLI values use the last one. Restart the viewer to apply changes. Future playtests use the configured default brightness without a CLI override unless the user requests one. Headset brightness/comfort acceptance remains pending.


## Behind-view capture investigation (read-only)

HOTD2 submits real XYZ before GPU projection, so VC2's extra rotated-camera passes may be unnecessary. The existing Mdl_Draw side-plane relaxation does not expand Stage_DrawSection's whole-object visibility gates. Installed-EXE inspection identified Stage_DrawSection 0x401260 calling 0x406310 at 0x40137B, 0x4013C7 and 0x40140E. 0x419200 retrieves a bounding sphere; it is not the visibility test. Mdl_Draw's sphere check at 0x4A7F81 still retains near/back/custom rejection.

First candidate prototype: expand only valid loaded stage-model submissions, including their deferred second pass, and allow rear stage meshes through the appropriate native camera checks. Preserve explicit stage omissions, face culling and native draw/material passes. Stage-task scope alone does not cover deferred submissions. Measure rear vertices, dropped geometry, queue capacity and CPU time before broadening it. No prototype or runtime change was made by this investigation.

A cache cannot invent never-submitted geometry. Consider it only for scenery lost when sections switch/unload (native format notes: `C:/src/gaming/hotd2-re/notes/formats.md:273`). Safe retention needs owned static geometry in stable coordinates, protected texture lifetime, bounded retention and section/stage/reset invalidation. Current output is camera-space and atlas tiles are reused; retaining it blindly can produce wrong textures, frozen lighting, stale walls or enemy ghosts. Prefer known stage-model ownership over VC2's repeated-position static heuristic. Rear coverage, authored backfaces, performance and section transitions remain untested.


## Native crosshair investigation

The initial bridge had no precise native-crosshair filter. A viewer comment claiming removal of VC2 texture IDs 1361..1376 was inherited donor text. The legacy positional `hidecross` heuristic defaulted disabled and compared only P1's aim, so it was not a reliable P2 removal path. The separate `crosshair` setting controls generated VR aiming graphics, not these native sprites.

Native selection uses the eight WORD entries at `0x579F58`, indexed by player and saved style (`0x9C9F60 + player*0x7C`):

| Player | Style 0 | Style 1 | Style 2 | Style 3 | Native definition |
| --- | --- | --- | --- | --- | --- |
| P1 | `0xAA8` | `0xAAA` | `0xAAB` | `0xAAC` | `0x9A5D44` |
| P2 | `0xAA9` | `0xAAD` | `0xAAE` | `0xAAF` | `0x9A5E74` |

Hud_UpdateCrosshair `0x4169C0` draws in game phase 6 when lives and shooting permission are positive and either the gun-device mode is 5/6 or a non-mouse player has valid gun input. A read-only snapshot of the user's running game (PID 9556, viewer 86036) found both players active with three lives, valid aim and shooting enabled. P1 used device mode 6/style 0 at native screen position `(1614,110)`; P2 used device mode 3/style 0 at `(322,274)`. P1 was outside the original 640x480 view while P2 was inside it, consistent with the reported P2-only blue square. This does not show that P1 has a working filter.

In that snapshot, definition IDs `0xAA8`/`0xAA9` resolved through `0x57D448 + id*4` to bitmap handles `0x1365`/`0x1366`; the live texture table at `[0x810A0C]` resolved them to surfaces `0x0DC7A888`/`0x0DC7A868`. Handles, table bases and surface addresses are runtime evidence, not stable filter constants. Options previews use the same crosshair artwork, so texture-only suppression could remove that UI too.

A narrow capture-only suppression candidate is Bitmap_DrawDef 0x41C630, scoped by native caller and player crosshair definition identity. Verified first nine bytes are `8B 44 24 04 83 EC 38 8B 08` (complete instructions). Gameplay call 0x416AE2 returns to 0x416AE7; end-screen gun crosshair call 0x4169B1 returns to 0x4169B6. Both pass player+0xE4 (P1 0x9A5D44, P2 +0x130). Options preview reaches the same renderer via generic helper 0x488000, return 0x488081 with a temporary definition; caller/definition guards can preserve that preview and unrelated HUD. Any implementation must preserve native desktop rendering and VR aiming indicators. Native captured-screen panels can still contain the original crosshair, so that path needs separate consideration. No hook or suppression was implemented by this investigation.


Crosshair queuing constraint: Bitmap_DrawDef can enqueue rather than draw immediately (0x4ACD20 -> Sprite_Submit 0x4A7A40, copied 0x3C record). Therefore a scoped capture_skip only around Bitmap_DrawDef would miss deferred output. A future solution must retain precise crosshair provenance through the queued record and apply capture-only suppression during Sprite_Draw 0x4A7AB0, while still invoking native drawing. Its verified nine-byte detour signature is `81 EC B0 00 00 00 53 55 56`. Record+0xC carries the resolved bitmap handle, but handle-only filtering can also match preview/aliases; preserve caller/definition identity and clear frame-local tags. This is a candidate design, not runtime-qualified implementation.

The smallest proposed conversion-only change can reuse the existing Sprite_Submit hook: mark the immediate scope and exact allocated deferred record only for the native crosshair caller/definition, then omit its converted geometry during Sprite_Draw. Keep the native call running; preserve Options previews, other HUD and generated VR gun/laser/crosshair geometry. Regression coverage should include both players/all four styles, immediate/deferred draws, P2-only play, equal-value/recycled queued records, unchanged native draw calls and unchanged generated aiming vertices. Native framebuffer panels would still contain desktop crosshair pixels. Research only: no suppression hook, source change, live patch or restart was made during this investigation.

## Native gameplay crosshair suppression

The supported-profile bridge now scopes Bitmap_DrawDef by the two native gameplay/end-screen callers, exact P1/P2 definition addresses and each player's four native IDs. Immediate sprite ownership is scoped to its submission; deferred ownership uses the actual allocated queue address and all 60 post-copy bytes, consumed during Sprite_Draw. Conversion is omitted while the original native drawing still runs. Options previews, equal-value aliases and unrelated nested sprites retain their geometry. Tags clear at publication; saturation leaves unknown draws visible rather than hiding unrelated HUD.

The obsolete P1-only proximity heuristic and `hidecross` setting were removed from the viewer/default INI. Existing keys are ignored. A pullFrame regression with a preview quad at P1's aim failed against the old heuristic and passes after removal with identical vertex bytes. Generated VR aiming graphics and their independent `laser`/`crosshair` controls are unchanged.

Full build/offline/GPU checks pass (`build/crosshair-build.log`), including both players/all four styles, gameplay/end callers, immediate/deferred submissions, original native calls, Options/shared artwork, nested skip restoration, edited/recycled queue addresses, saturation, frame reset and profile rejection. Independent review found no blocker. A missing D3D7 enum include in the concurrent book regression was added; that regression now builds and passes too.

Owned windowed native smoke PID 85848 (`build/crosshair-live.json`, `build/crosshair-live-bridge.log`) reached both players active/status 5 and recorded the bounded actual-suppression log. The native screenshot `build/crosshair-native-desktop.png` retains P2's blue crosshair, as required for unchanged desktop rendering. Four volume-control cases also returned successful real DirectSound calls on open music/effect/voice buffers with zero recorded volume errors; this does not establish listening quality or device-loss recovery. All five convenience cheats were enabled at 10 shots/sec. The game closed normally and the installed executable hash remained unchanged. No VR session was launched.

The combined current protocol-15 binaries, including pending audio and book changes, are now built in the normal `build` directory. The active INI was preserved. The previous matched binaries/config/log are backed up at `build/before-crosshair-20261018-165750/`. Headset and real Options/end-screen visual acceptance remain pending. Native framebuffer panels still mirror original desktop pixels, including any crosshair; suppression applies to converted geometry.

## Configurable mesh and loaded-stage visibility

`[vr] expand_meshes=1` preserves the existing normal Mdl_Draw side-plane expansion for offscreen meshes, including separate body parts; `0` restores its native rejection. Alternative-model draws retain native rejection by default. `[vr] expand_world=0` defaults disabled; `1` enables experimental expansion of valid loaded static stage submissions beyond the original camera, including rear meshes. This is a submission/culling change, not a retained-geometry cache or rotated-camera rendering pass.

A centralized ComputeSphereVisibility policy replaces the fixed normal-model mask patch. Normal expansion clears only original-camera side reject bits. Expanded stage draws also clear original-camera near/far rejection so geometry behind the original view can reach conversion. Custom planes remain, along with material passes, native face culling and the viewer depth buffer. Bounded stage-provenance tags follow deferred model submissions; missing/overflowed provenance falls back to the configured normal/alternative mesh policy rather than treating unrelated models as stage scenery. Explicit stage omissions, unloaded geometry and unauthored rear faces remain outside this expansion.

Viewer flags `--expand-meshes` / `--no-expand-meshes` and `--expand-world` / `--no-expand-world` override the INI for one launch; last direct flag wins. Start-HOTD2.ps1 forwards `-ExpandMeshes` / `-NoExpandMeshes` and `-ExpandWorld` / `-NoExpandWorld`, rejecting opposing switches. Restart the viewer after changing settings. In the existing protocol-v12 render_flags word, HOTD_RENDER_NATIVE_MESH_CULLING=2 requests native mesh rejection and HOTD_RENDER_EXPAND_WORLD=4 requests stage expansion. Vertex/frame layout stays unchanged.

No native forward-view wall parity, full rear coverage, section-transition behavior, headset acceptance or frame-time improvement is established by this implementation. Expanded submissions can increase CPU work, queue usage, vertex counts and live texture demand. Check dropped geometry and provenance capacity alongside rear coverage before enabling world expansion by default. No VR launch was requested for this work.


Visibility validation: full `build/capture-expansion-build.log` passes, including fake-COM normal/alternative policy, every native/custom sphere plane, scoped/nested model calls, deferred alpha-record copies, overflow, signature guards and failed native API results; INI/CLI/launcher flag guards and existing lighting/HUD/input/cheat checks also pass. Independent review found and corrected deferred ALT alpha rewriting before integration.

Owned windowed native session PID 84716 confirmed active hooks and reversible live flags. The initial sampling started during black startup frames and is not useful for coverage. A second pass waited for stage geometry; `build/capture-expansion-live.json` records an unpaused street scene. Repeated default samples contained about 29.6k world / 4.5k rear vertices; expanded world about 34.8k world / 9.6k rear vertices. No dropped triangles, atlas failures or stage-tag overflow were observed. Native publication ran about 49 FPS default and 42 FPS expanded in that short run, indicating extra work; actors/time were not frozen, so this is not a controlled VR performance benchmark.

Offscreen replay of the same expanded packet shows forward and 180-degree rear views: `build/capture-front.png` / `build/capture-rear.png`. Rear street/building geometry is visible; the sampled native forward screenshots show no obvious extra wall. This does not establish pixel parity, all sections or headset acceptance. The owned game closed normally and the installed Hod2.exe retained its supported SHA256. No VR was launched. World expansion stays off by default.

Subsequent authorized headset test (`build/world-expansion-vr.log`, game PID 91376, viewer PID 85256) enabled mesh/world expansion, brightness 80%, all convenience cheats and rapid fire at 10 shots/sec. User confirmed rear scenery appears but reported jerkiness and visual artifacts. Keep `expand_world=0`; this remains an experimental option, not accepted full-scene coverage. User then requested default brightness 90% and future tests at the configured default without a brightness override. No additional VR launch was requested with those default changes.

## Culling review and hardening

The [deep culling review](culling-review.md) documents repairs for aliased stage provenance, invalidation of accepted tags on saturation, unchecked shared native queue appends, disabled-path work and excessive pressure logging. Frame-latched flags preserve immediate/deferred policy. Draw-local strip/fan conversion reuse reduces the 100-vertex example from 294 lighting evaluations to 100 with identical captured bytes; this is not a measured speedup or a confirmed explanation of the headset artifacts.

Full build/offline/GPU checks pass (`build/culling-review-build.log`), with independent review complete. Native gameplay validation after a 20-second startup wait (owned PID 17560, `build/culling-review-live.json`) passed seven flag sweeps with nonzero geometry, no dropped triangles and no sampled queue/tag/texture failures. Defaults captured about 29.5k world / 4.3k rear vertices; world expansion about 34.8k / 9.6k. Moving-scene timing is not a controlled benchmark. The game closed normally, no game/viewer remains running, and the installed executable hash is unchanged. Defaults remain world expansion off and brightness 90%; headset retesting is pending, with no new VR launch performed.

## Game audio volume

Added `[vr] master_volume`, `music_volume` and `effects_volume`, all default 100 with integer range 0..100. Zero mutes; 100 preserves the original level. Master multiplies the selected category's linear gain; effects include gunshots, voices and other non-music audio. The controls target HOTD2's DirectSound audio, without changing Windows or other applications.

Viewer overrides are `--master-volume N`, `--music-volume N` and `--effects-volume N`; Start-HOTD2.ps1 exposes `-MasterVolume`, `-MusicVolume` and `-EffectsVolume`, validates 0..100 and forwards only explicitly bound parameters. For example, `-MusicVolume 0 -EffectsVolume 0` mutes both categories without writing the INI. Restart the viewer after INI changes and use matching bridge/viewer builds.

Native channel playback at 0x4A34B0 installs a buffer Play hook so attenuation applies after native restoration and before samples start. Channel 15 is music; the other 20 channels use the effects gain, including voice channel 16. Absolute DirectSound attenuation avoids compounding on duplicated buffers. The frame-thread refresh updates already-playing buffers; transient failures retry with bounded diagnostics. Neutral controls retain native playback on ordinary control failure. Per-launch packed audio levels append four bytes in protocol 14; matching bridge/viewer builds are required. CLI overrides apply on viewer connection and persist for that game process.

Full build/offline/GPU checks pass in `build/audio-stage` (`build/audio-stage-build.log`), including fake DirectSound playback, independent categories/master, mute, duplicate/reused buffers, lost-buffer retry, live-music failure recovery, strict INI/CLI validation and existing input/graphics/pause regressions. Independent review found no remaining blocker. PowerShell parameter/explicit-only forwarding checks and Python live-test syntax pass. The initial audio build was staged while the existing game/viewer were running. After that session closed, the combined normal build and owned native smoke documented above validated successful DirectSound volume updates. Listening and actual device-loss verification remain pending. Native crosshair implementation is documented separately; generated VR aiming indicators are unchanged.

## G's File presentation depth

The user identified the animated boss briefing book as uncomfortably close in VR. The paused first-boss capture under `build/boss-book-inspect-20261018-164805/` contains book cover/page vertices at native camera depths 0.747..1.086 units, before viewer scaling. These models took the world capture path, bypassing the two-metre HUD canvas. The book is G's File, the boss weak-point briefing, rather than an ordinary scene prop. Its model geometry signature also appears in the six boss polygon archives.

Static tracing identifies the parameterized story briefing task at `0x437AC0` and the Boss Mode book task at `0x424900`. Both animate eight cover/page submissions, including a boss-specific illustration. The bridge validates both task signatures and marks only model submissions owned by these tasks. The existing exact deferred-record tracker preserves that ownership through native queue copies and alternate-alpha rewriting; equal-value records at other addresses cannot inherit it, and tags clear each frame. The native game still receives the original draw calls and geometry.

Protocol 15 adds `HOTD_GFILE` provenance in unused address metadata without changing the protocol 14 byte layout. The viewer scales the book's complete XYZ geometry from its native one-unit presentation plane to a nominal one metre, independently of world scale. This preserves projected size, page animation and relative 3D depth. It retains ordinary scene depth testing and writing; it does not flatten the pages or force them above other geometry. Briefing geometry is excluded from aiming and shot collision. Ordinary HUD elements remain at two metres. Tutorial elements and other camera-relative models are not reclassified.

The full build and offline checks pass in `build/gfile-validation/build.log`, with unchanged build-input hashes recorded in `source-hashes.json`. The new viewer depth test fails against a separate copy of the original viewer at the book-distance assertion (`before-test.log`) and passes with the correction. Actual bridge tests cover both task signatures, immediate/deferred/ALT submissions, nested draws, record aliases and mutation, disabled-profile fallback and frame cleanup. Viewer tests cover 1/1.25/2x world scales, repeated snapshots without cumulative scaling, unchanged projection ratios, book material data and depth state, fixed HUD placement and aim exclusion. The build is staged separately from the live game; this work did not restart or modify the game/viewer. Loading the matched protocol 15 pair and checking page animation, overlap and comfort in the headset remain pending. Source backups and the pre-existing patch are under `build/gfile-backup-20261018-165710/`; restore only this change's hunks when rolling back, preserving unrelated work.


## Confirmed shot rumble

Protocol 16 appends two monotonic per-player shot counters; existing offsets stay unchanged. The bridge validates the native `Shot_BuildRay` entry at `0x406110` and its two call sites (`0x414A2C`, normal; `0x414D6C`, Original weapons), then counts completed calls during unpaused gameplay. Native shooting checks happen before these calls, so an input edge alone, an empty pull or a reload does not create a feedback event. The seven-byte trampoline contains two complete, non-relative instructions and preserves the original arguments/call. Unsupported signatures leave shot rumble disabled and log that result.

The viewer copies counters under the frame seqlock and maps P1 to the right hand, P2 to the left. Each controller gets at most one fixed-strength 30 ms OpenXR pulse per 100 ms. Counter changes during rate limiting, disabled haptics, pause or controller tracking/sync loss are consumed without queuing; attach/refocus seeds the current counters. Firing rate, weapon timing and cheat settings are untouched. The desktop preview sends no haptic calls.

Offline regressions cover native signatures and original-call preservation, both players/modes, non-gameplay and pause guards, counter publication/snapshot rejection, hand mapping, no feedback from input alone, 30 ms pulse duration, 10 Hz limiting, no deferred bursts, independent hands, disabled/tracking-loss behavior, refocus seeding and clock/counter wrap. The old trigger-edge implementation fails the input-without-shot regression. Actual headset feel, Original weapon runtime behavior and measured performance remain unverified; no VR launch was performed for this change.


## Player HUD ownership and position

Protocol 18 integrates four groups in unused vertex address bits 9..11 with the separate VR Pause and Reload/Cancel inputs. It rejects the incompatible protocol-17 builds from the separate branches. Groups identify P1/P2 ammo and P1/P2 lives. The bridge validates and wraps `Hud_DrawAmmo` (`0x4177D0`, seven-byte complete prologue) and `Hud_DrawLives` (`0x4174A0`, six-byte complete prologue), reading player index from the supplied task at +0x34. It preserves native calls, arguments and the lives return value. It tags converted sprites using the existing submit/draw path, including exact allocated deferred-record addresses and full 60-byte identity. Crosshair and HUD ownership now share the existing record tracker without changing its behavior. Tags clear at frame publication; unsupported profiles or lost ownership leave original capture positions.

The viewer applies `[vr] swap_player_hud=1` independently of akimbo, translating each complete ammo/lives row so its bounding-box center moves to the opposite side of the existing HUD canvas. Vertex order, UVs, relative spacing, digits, Y/depth and materials remain intact. Labels, Original weapon numbers and reload prompts are part of their native row. Rows right/left align as their width changes; no texture filtering or removal is added. Source/shared vertices and native desktop rendering are untouched. Framebuffer panels still show native layout. Controller mapping remains right P1 / left P2.

The pre-change snapshot-reader regression fails at the opposite-side assertion. Focused native tests cover both player/task kinds, immediate/deferred capture, original-call/return preservation, aliases, mutation, invalid players, unsupported profiles and frame cleanup. Viewer tests cover flag/akimbo combinations, both players/groups, repeated snapshots, 1/1.25/2x world scales, stable materials/depth, retained captions/world/book/unknown groups and INI/CLI overrides. The user confirmed akimbo placement in the headset. The follow-up regression requires swapping with either akimbo value and fails against the previous akimbo-gated implementation. Non-akimbo runtime placement, transitions and authored HUD variants still need confirmation. Main integration rebuilds the matched protocol-18 bridge/viewer and preserves the newer control mapping. Full build/offline/GPU checks pass in `build/hud-merge-build.log`; the combined build has not been relaunched in VR.

## Experimental Arcade scene checkpoints

The `feat/stage-checkpoints` worktree adds CLI-only starting stage/scene selection and opt-in `[vr] continue_game=1` recording/resume. The launcher passes explicit overrides to the injected child through private environment variables, clearing inherited values when absent. The initial stage/resume path required no shared-frame layout change; manual requests below add protocol 19. Original, Training, Boss and demos retain their native entry paths. Recording saves only stage and scene, not exact script position, variant, player state or route history. A new Arcade start after the menus selects the requested/saved scene; it does not load a full save state.

Three native CALL sites are validated before patching: `Game6_Start` table loading at `0x45FF0A` (original target `0x413160`), stage initialization at `0x45FFD4` (`0x460030`), and root event initialization at `0x460710` (`0x45EBC0`). Wrappers preserve native calls and stage-start arguments. The selected table is loaded before its resources initialize. Scene lists are bounded and checked through the relocated event tables, including the Arcade variant-1 entry, before use. A rejected scene falls back to native chapter 1. Chapter entrances use `{0,0,7,0,0,0}` for chapters 1..6.

An initial chapter-1 scene-3 desktop test reached the right scene and emitted geometry, but the user observed characters without the world. Its gameplay variant 1 assumes earlier setup. Native scene variant 0 begins with section-6 preload, request flush and section switch (`0x28`, `0x59`, `0x29`), plus environment/resource setup. For explicit/resumed mid-scene entries, the event-init wrapper now selects that native setup variant when available, leaving its ordinary transition to variant 1 intact. Chapter entrances keep native variant selection. The user confirmed the subsequent scene-3 test restored the visible world. This is scene-specific confirmation, not complete scene compatibility.

`HOTD2VR.checkpoint.ini` lives beside the injected bridge and contains `[checkpoint] version=1`, one-based stage and zero-based scene. Active Arcade gameplay (mode 6/sub-state 2, active player) records only changed locations, after frame publication releases the seqlock. A flushed temporary file replaces the previous checkpoint atomically; failures preserve the prior file and retry after five seconds. Default-off configuration installs no scene-entry hooks and makes no automatic checkpoint writes; manual saving is handled separately below. Direct CLI overrides win over saved location; disabling resume/recording preserves the file. Existing native saves and executable bytes are not patched on disk.

Offline tests cover strict CLI/file parsing, defaults and precedence, stale environment clearing, all chapter entrances, native callback preservation, variant-0 bootstrapping, script/pointer bounds, signature rejection before any call changes, non-Arcade guards, inactive/menu/continue guards, location-change-only writes, failed replacement retention and retry. PowerShell forwarding/validation is exercised through non-launching executable shims; startup options reach the injector rather than the viewer. Full build/offline/GPU verification is recorded in `build/checkpoint-build.log`.

Owned windowed native tests use all five convenience cheats at 10 shots/sec, with no VR viewer. Artifacts are `build/checkpoint-*.json` and matching logs. Cases reached chapter 6/scene 0 and resumed it across process restart, honored a chapter-2 CLI override with recording disabled and unchanged saved file, retained normal chapter-1 startup, entered chapter 3/scene 7, and rejected chapter 6/scene 255 back to chapter 1. The corrected chapter-1/scene-3 case loads section 6, records the location, and resumes it on a new process using only the INI toggle. One attempted launch was refused by the existing shared-mapping guard; later tests waited for the old mapping to disappear. No VR launch was performed. All scenes, route prerequisites, later-stage objects, measured disk-write frame impact and exact-state restoration remain unverified.

## On-demand checkpoint hook

Protocol 19 appends five 32-bit fields at byte 87,385,016: `checkpoint_request`, `checkpoint_response`, `checkpoint_result`, `checkpoint_stage` and `checkpoint_scene`. Frame size becomes 87,385,036; previous offsets remain unchanged. A future caller serializes save requests, increments `checkpoint_request`, and waits until `checkpoint_response` equals that request ID. The bridge handles it on the game thread after releasing frame publication's seqlock. It publishes result/location before acknowledging the request. Results are `HOTD_SAVE_OK`, `HOTD_SAVE_UNAVAILABLE` or `HOTD_SAVE_IO_ERROR`. The manual path works independently of automatic recording or scene-entry hooks, including while native gameplay is paused.

The bridge request handler remains the only production on-demand save entry point. Initial viewer/PowerShell CLI adapters were used for testing, then removed at the user's request: the intended deliverable is the bridge hook for future callers. No save CLI, UI, menu, controller binding, live load or scene jump is added. Calls from a read-only preview must use a separate writable connection rather than writing its read-only mapping. A caller timeout must not claim success; an already submitted request can still complete later.

The bridge validates active Arcade mode/sub-state, player activity and scene-script availability before saving. It writes the same atomic checkpoint file, updates its last-saved location on success, and acknowledges unavailable scenes and I/O failures without replacing the previous checkpoint. If automatic recording remains enabled, future scene changes can overwrite a manual save. The file still contains only stage/scene; full game-state restoration is not implemented.

Full build/offline/GPU checks passed before adapter removal (`build/manual-checkpoint-build.log`); the hook-only rebuild is recorded in `build/checkpoint-hook-only-build.log`. Retained native callback/file regressions cover manual saving with automatic recording and scene hooks disabled, pause eligibility, unavailable/menu requests, preservation on failed writes, acknowledgments and no duplicate service of the same request. Temporary viewer/PowerShell adapter tests were removed with those adapters; bridge behavior and its protocol remain unchanged.

After user authorization to close the separate vr-menu session, owned desktop-only tests rejected save requests at the title/menu without altering the old file, then successfully saved chapter 1/scene 4 while paused with autosave off. Native logs confirm section 7 and the manual save acknowledgment (`build/checkpoint-manual.log` / `.json`). A second default-start case, with no scene-entry hooks and autosave off, saved chapter 1/scene 0 (`build/checkpoint-manual-default.log` / `.json`). Both used all convenience cheats at 10 shots/sec and closed their owned game afterward. No VR launch occurred for this addition; headset regression, all scene prerequisites and measured write latency remain unverified.


## Runtime Save game and Load game integration

Protocol 20 extends the protocol-19 foundation with `checkpoint_action` at offset
87,385,036 and heartbeat-owned `checkpoint_autosave` at 87,385,040. Frame size is
87,385,044. All previous offsets stay unchanged. Actions are Save (0), Load (1)
and New game (2). Results additionally include invalid destination (4) and load
initialization failure (5). The existing settings panel invokes this hook;
there is no save CLI. Autosave updates through the existing heartbeat, with
startup/CLI configuration restored after heartbeat expiry.

Loads preflight the saved destination against bounded native `comevtbl.bin` and
`stNevtbl.bin` files using the native pointer relocation. Before mutating game
state, each scene/variant/script pointer must be aligned, in a loaded file's
bounds and lead to an advancing Arcade opcode. This checks structural validity,
not dependencies on earlier scripts, actors or route state.

The bridge queues native mode request 6 at `0x7C17A0`; `Game_ApplyState`
(`0x40E860`) performs task/resource cleanup and resets the mode/sub-state before
normal `Game6_Start` initialization. The existing scene-entry hooks prioritize
the pending Load/New target over CLI/auto-resume. New game selects chapter 1,
scene 0. Existing players restart in their native joining state; title/menu
loads choose P1, or both with akimbo. Paused loads request the existing native
pause predicate so the native pause routine resumes audio/resources itself.
No direct task teardown or pause-flag assignment is used.

Load ACK requires destination root initialization, active Arcade sub-state 2,
and matching stage/scene. Until then autosave is suppressed. Bridge timeout is
30 seconds; viewer timeout is 45 seconds and cannot release an outstanding
request. Invalid-file rejection occurs before native state changes. Later native
initialization failure cannot promise restoration of the previous scene.

Native desktop smoke tests passed paused Save/Load at chapter 1 scene 4,
cross-chapter Load to chapter 2 scene 0, New game despite explicit startup scene,
title-screen Load with autosave off, live Autosave on/off and invalid scene 255
rejection. Tests restore the save and leave the personal INI unchanged. Build,
controller/menu regression and WARP rendering checks passed. Headset UI/load
integration and full scene prerequisites remain unverified.


Protocol 21 adds read-only current stage/scene at offsets 87,385,044/48 and
saved stage/scene at 87,385,052/56; size 87,385,060. Stage zero marks unavailable.
The bridge publishes these fields under the existing frame seqlock. Saved-file
reads occur outside publication, once per second or after a successful write;
current location comes from the same Arcade guard as Save. The viewer displays
the selected Save/Load destination before confirmation, without issuing RPCs,
changing the game, or reading native game addresses. User confirmed the pre-selection Save/Load captions in VR at stage 3, scene 11. Full scene prerequisite coverage remains unverified.


## Reduced shot sparks

The original shot-depth fix (`3c459e3`) preserved projected spark size while
moving camera-plane flashes onto world surfaces. Shot flashes now render at
35 percent of their original width and height, contracted around their exact
native shot anchor after depth placement. Surface depth, hit position, both
hands, animation-layer alignment and aimreach fallback remain unchanged.
Native desktop rendering, world impacts/blood and traveling tracers are unaffected.

The actual viewer regression failed before the change and passes afterward,
covering both off-center guns, reduced width/height, unchanged anchors/depth,
asymmetric animation layers, fallback placement and untouched world geometry.
Viewer build, snapshot/controllers, world scale, aim indicators, WARP rendering,
menu and output-ownership checks pass. No game/VR launch; headset size acceptance
remains pending. The initial 50-percent setting was subsequently reduced to
35 percent at the user's request; the running 50-percent VR session was retained.

## Native graphics with independent bridge services

`Start-HOTD2.ps1 -NoWindow -NoVrWindow` retains original graphics and native controls while keeping checkpoint, cheat and audio services. The launcher resolves the existing INI output settings and CLI overrides, including Stereo, before injection and passes the capture decision through a private child environment value. With both outputs off, the bridge skips graphics capture, lighting reconstruction, visibility expansion, bitmap alpha repair and native pause replay. The native input update publishes checkpoint state and services requests, cheat restoration and audio updates without accessing a D3D device or changing the presentation callback. The native pause predicate passes through except when a Load request needs native unpause. Invincibility/audio hooks remain independent of rendering.

Explicit stage/scene selection and INI checkpoint resume keep their Arcade guards and scene bootstrap. Chapter 4, scene 0 is the user's bat-wing comparison destination. Checkpoints restart a scene; they do not restore exact bat positions, animation or prior route state. The viewer's existing headless loop publishes cheat/audio settings and heartbeat without D3D11/OpenXR, loading menu glyphs, writing menu settings or publishing synthetic aiming. Rapid fire still requires VR trigger input; physical mouse auto-fire is not added. Capturing versus noncapturing bridge installation changes require a game restart; standalone viewer reconnection does not install/remove graphics hooks. `--native-window` stays uninjected.

Offline startup/checkpoint checks cover output combinations, INI/CLI precedence, Stereo, stale environment replacement, native pause pass-through, servicing without a D3D device, cheat heartbeat expiry and unchanged captured graphics. A full fake-image comparison permits only the pause and input entry changes in the service installer, requires the presentation pointer and remaining code to stay native, and checks signature rejection. PowerShell tests use temporary launcher/service shims, never the game, and verify that output flags reach both processes while checkpoint/cheat/audio flags reach their existing owners. The actual viewer headless loop is tested against a separate mapping, including heartbeat, no graphics device, untouched aiming and exit cleanup. Native/headset runtime qualification remains pending; the user's paused game and loaded binaries were preserved while building into a separate staging directory.

## Targeted bat trace

The user confirmed white, apparently detached bat wings only in the conversion run; the native-only chapter-4 scene-0 run looked correct. Its resident wing artwork and the earlier conversion atlas tile match every RGBA byte. Live model normals had unit length, wing material specular was approximately 0.29/0.29/0.31 with power 8, and the earlier captured white triangle had saturated specular. These observations do not yet identify the lighting cause or prove actual geometric displacement.

The opt-in `trace_bats` diagnostic scopes native body/wing model submissions by the verified loaded slots, restores scope across nested draws, and copies the exact draw inputs and converted outputs. It samples up to four unpaused frames 15 frames apart, bounds allocation and per-draw copies, and flushes once at completion, pause or scene exit. Native framebuffer copies use the successful screen-publication sequence, independently of the geometry-frame counter. Output metadata contains compiler layout offsets so the standard-library reader can validate the binary and compare captured positions against native world/view transforms. No shared protocol or renderer behavior changes; graphics-disabled launches skip tracing entirely. Unit tests exercise disabled/stage/scene/pause guards, sample spacing, truncation, input preservation and completed one-shot output. New trace session runtime evidence is recorded separately under ignored build artifacts.

The subsequent focused OpenXR run succeeded. The user confirmed excessive intermittent specular and detached-looking wings, with visible artwork, and supplied a screenshot. The bounded trace reached its 256-draw cap over two sampled frames (1449 and 1464); no individual draw was truncated. All 256 expanded triangle sequences match the traced native inputs transformed by the device world/view matrices, with maximum positional error below 0.000004 game units. This verifies capture conversion for those submissions, not native rasterization, attachment poses, visibility or downstream rendering.

Transformed wing normals have length approximately 1.42857 under a 0.7 world scale, with NORMALIZENORMALS disabled. Recomputing the current CPU specular equation finds 57 wing-input examples exceeding 1 in every RGB channel before clamping; one has N dot H 1.25135, power 8 and specular RGB 1.46456/1.46456/1.56557. This provides a concrete lighting lead, but native D3D7 behavior still needs comparison before choosing a correction. The detached appearance remains unexplained and is also present in paused native replay, so it is not restricted to stereo presentation.

This first live trace omitted exact-frame BMPs because it incorrectly equated independent screen and geometry counters. The freshness check is corrected and regression-tested in source and a separately staged DLL; the running session was retained. Saved paused images and the user's screenshot are later references, not matching images for the two sampled frames. Evidence is under `build/bat-trace-stage/run-20261018-195701/`. No graphics repair or headset acceptance is claimed.

An offline Direct3D7 HAL device subsequently processed the same 1,900 native input vertices using the traced world/view/projection, materials, lights and render states. `ProcessVertices` agrees with the CPU screen positions to within 0.000077 pixels and gives identical packed diffuse colors. The original CPU specular differs at 242 input vertices, with wing errors up to 193/255 per channel (native 62/62/66 versus conversion 255/255/255). Capping N dot H to [0,1] before exponentiation reduces this to three differing inputs: one body input differs by 8/255 and two wing inputs by 1/255. Native approximation/quantization remains unmatched at those inputs; transformed normals and diffuse lighting are preserved.

The scaled-wing regression fails before the one-line shared lighting correction and passes afterward. Bat trace, vertex reuse, texture cache and real D3D7 pause replay suites pass with the corrected bridge. This repair addresses excessive specular, without a geometry or material override. A separately staged chapter-4 scene-0 VR run was launched for visual validation and matching native framebuffer capture; detached-wing diagnosis remains open.

The user confirmed improved highlights in that run, with wings still appearing detached. The new trace saved two matching native framebuffers (frames 1001/1016) and 256 draws. Its first sample contains 192 body strips (six bodies, 32 strips each) and 60 wing strips (six sets, ten strips each). Original skeleton kind 0x1E draws body 0x1B01; companion kind 0x1F uses six rigid wing parts 0x1B02/03/06/07/08/09. Models 0x1B04/05 are not referenced by that skeleton, so their absence does not establish missing geometry. The earlier suspicion of missing skinned attachments is unsupported for this active skeleton.

The last unpaused native-framebuffer reference in the hooked run also shows wing/body gaps at close range. This requires a close-range native-only comparison before changing poses or attachment geometry; the earlier user-confirmed native baseline did not record comparable actor distance/pose. The running game was subsequently closed. Current evidence and logs are preserved under `build/bat-trace-stage/run-20261018-200547/`; the corrected DLL is deployed only to the isolated trace staging runtime, not the normal build output. No wing-placement correction has been made.

## Bounded bat autoplay diagnostic (2026-10-22)

`tools/capture_bats.py` builds a diagnostic-only bridge with `HOTD_BAT_BOT`, reusing `hotd2-re/src/hook/auto_play.cpp` and its native gun calibration. Its only gameplay writes are processed P1 gun input through the existing native input hook; enemy deaths and scene progression remain native. It scopes shooting to Arcade chapter 4, scene 0, suppresses firing while paused, permanently stops at live bat detection, and requests capture at distance <=30. The bot has a 120-second in-scene timeout; the runner has a 180-second overall limit. Existing diagnostic traces wait until that close-range trigger.

Native mode installs gameplay services plus the diagnostic gun hook, with zero graphics hooks or D3D framebuffer access; its screenshot is taken by the external desktop runner. The initial native implementation used a presentation callback and framebuffer readback, and was superseded after the user required stronger isolation. Conversion mode also retains the existing bounded bat draw trace. Both save the complete bridge frame and raw body/companion actor records with decoded model IDs and node matrices; the runner also saves PNGs and a read-only desktop preview. It never enables VR/OpenXR, refuses existing game/viewer/SteamVR/PSVR sessions, and terminates only its owned test processes. Runtime INIs, checkpoint files and binaries are isolated under ignored `build/bat-bot-*`.

The actual RE selector passed a fake-game-memory test for a calibrated normal shot, stage/scene guards, pause suppression, permanent bat stop, distance trigger, body/companion dump layout and one-shot pause acknowledgment. The actor parser rejects incomplete/unsupported/trailing records. Checkpoint, bounded trace, real D3D7 pause replay and native-lighting regression suites pass. Diagnostic bridge and launcher compile successfully.

Initial live attempts stopped at the original game's “Please insert the 'The House of The Dead 2' CD.” dialog before rendering/gameplay. Disassembly at 0x4A6830 confirms a CD-ROM scan for Hod2.exe; F: had no disc. A stale registry Install Path was briefly tested, did not change this result, and was restored exactly; backup is `build/bat-bot-20261022-145252/registry-before.json`.

Following the user's request to reuse the VC2 approach, optional `[bridge] launchfix=1` now changes the conditional-branch displacement at 0x4A6858 from 0x0D to zero before the suspended game resumes. Both branch outcomes then reach the function's existing success return; only the CD-ROM scan is bypassed. The launcher verifies the nine-byte original signature at 0x4A6850, restores protection, flushes the instruction cache and verifies the write. Failure prevents startup. Tests verify INI default-off/enable, exactly one changed byte, unsupported signatures and restored protection. The rebuilt normal launcher and enabled build INI are backed up under `build/launchfix-backup-20261022-151432/`; the template defaults off. The installed game's SHA256 remains C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A.

With the patch active, native and conversion runs each fired 12 normal shots with 12 reported native hits, stopped firing at bat spawn and captured at 29.6792 game units. Each saved four body actors and four companion actors. Native evidence is `build/bat-bot-20261022-151257/native/`; converted evidence and paired summary are `build/bat-bot-20261022-151353/`. All 448 captured node-matrix values match exactly across those runs, and the native framebuffer bat crop (220,90)-(460,260) is pixel-identical. The conversion trace contains 168 complete draws with maximum capture-transform error below 0.000004 units. The original-selector aim-feedback check runs after native input refresh in this adapter and reports mismatches; it is not a reliable shot-coordinate check at this hook timing. Native hit feedback and actual scene progression succeeded.

Visual inspection of the graphics-disabled native framebuffer shows the same white wing triangles and body/wing gaps. The converted preview retains the gaps with less saturated lighting. This initial baseline still installed a presentation callback and performed a D3D readback. It does not rule out graphics-hook contamination and must not establish an unhooked native result. No wing-placement repair has been made. Both game sessions and their viewers were closed; final process checks found no HOTD2, viewer, SteamVR or PSVR processes. OpenXR was never enabled.


## Native baseline with zero graphics hooks (2026-10-22)

The user clarified that cheats and bot may remain, but no graphics hooks are acceptable for the native baseline. `Start-HOTD2.ps1 -NoGraphicsHooks` / launcher `--no-graphics-hooks` now forces capture off regardless of INI/output selections. The same noncapture path also serves `-NoWindow -NoVrWindow`. Checkpoint, cheat and audio servicing runs from native `Gun_UpdateRecords` instead of the presentation callback. The original presentation pointer, DirectDraw/Direct3D interfaces, model/strip submission, culling, textures, lighting and pause rendering are untouched. Native windowed mode selection and CD/splash startup patches remain explicit startup differences. Normal builds do not include the diagnostic autoplay bot.

The diagnostic native path no longer calls `capture_screen`, obtains a D3D device, or requests pause after saving actors. It writes only native actor/shared-state data; the Python runner obtains a desktop window screenshot externally. Actor matrices and screenshot can represent different frames, so this output must not be described as a synchronized pixel comparison. The earlier pixel-identical comparison remains evidence from a less isolated run, not proof that graphics hooks cannot cause the defect.

The input-driven service regression failed before the change and passes afterward. Whole-image installation checks allow modifications only to the pause/input predicates, verify both signatures before changes, and preserve the original presentation pointer. Startup/CLI tests cover the explicit flag overriding positive output/Stereo options and the PowerShell wrapper keeping its service viewer headless. Checkpoint, trace, real D3D7 pause replay, audio, protection and reload regressions pass.

A fresh native diagnostic reached chapter 4/scene 0, fired 12 normal shots with 12 native hits, stopped at bat spawn and saved four body/four companion actors at distance 29.6792. Graphics fields remained empty (zero vertices, screen dimensions and screen/atlas sequences). A read-only audit compared all 0xC2520 bytes of the running game's text section with the original file: differences were confined to known input, pause, checkpoint, damage/audio and startup patches. No graphics submission code changed; presentation pointer remained 0x4BA3FF. Evidence, external screenshot and audit: `build/bat-bot-20261022-151926/native/`. The external screenshot shows a later bat pose than the actor snapshot; no wing root cause or repair is claimed. Test game and service viewer were closed afterward; no OpenXR/SteamVR/PSVR runtime was started.

The rebuilt normal bridge (without autoplay) and launcher are deployed to `build/`. Previous binaries are preserved under `build/no-graphics-backup-20261022-152221/`; restoring those two binaries rolls back this change. No game files or runtime INI values changed for this mode.

## Gameplay join and credit HUD ownership (2026-10-22)

The native credit-line routine at `0x406CE0` takes a player index and draws that player's PRESS START/INSERT COIN prompt plus CREDIT/FREE PLAY row. Ammo/lives ownership hooks did not cover it, leaving the inactive player's prompt on the active player's hand side when HUD swapping was enabled. The bridge now tags this routine's immediate and deferred sprites as groups 5/6 during gameplay mode 6; the viewer applies the existing complete-row translation. Groups 1/2 and 3/4 remain ammo and lives. Protocol 21 layout stays unchanged. The separate global attract prompt at `0x4067D0` remains untagged.

The hook validates all seven original bytes (`83 3D 98 8E 9C 00 06`) before installation, alongside both existing HUD signatures. Regression checks cover both player indices, immediate/queued prompt and credit sprites, unsupported signatures, invalid players, non-gameplay modes, profile-disabled capture and frame reset. Viewer checks cover all six groups with swapping on/off, akimbo on/off and three world scales, preserving glyph order, UVs and source snapshots. The matched bridge/viewer passed all 19 offline checks and was deployed with backups under `build/hud-swap-backup-20261022-200823/`. The user confirmed the gameplay join/credit HUD swap works in VR. This confirms the tested session, not exhaustive mode or transition coverage.

## Diagnostic logging defaults

The older render-state sampler and per-shot/reload logging are now explicitly opt-in with `[bridge] trace_render_states=1` and `trace_input=1`; both default off and require a game restart. Disabled render-state tracing performs zero diagnostic D3D queries; the enabled sampler retains its every-30-frame/32-distinct-state bounds. Input tracing does not change gun/reload handling. Regressions exercise disabled/enabled D3D query counts and actual trigger/reload callbacks with captured logs. Ordinary startup, error and periodic health logs remain. Bat capture retains its independent default-off `trace_bats` flag, and autoplay/full-frame dumps require the separate diagnostic build. The sparse template omits all three trace keys; their built-in defaults keep tracing off. No wing cause or correction is claimed.


The following feature-branch evidence predates the protocol-24 merge; protocol
22/23 references describe those historical builds.

## Runtime stage and scene selection

Stage select follows Load game in the existing VR Settings root. It opens six
named chapters, then a paginated list of native scene IDs. Selecting a chapter
only browses; confirming a scene requests its restart. Back returns to chapters,
then the root. Chapter 3 defaults to its native entrance, scene 7. Other chapters
default to their entrance when available. Missing catalog data reports no scenes.
Companion previews can browse but cannot start a selected scene.

Protocol 22 appends request stage/scene at 87,385,060/64 and six immutable
256-bit native scene catalogs at 87,385,068; frame size is 87,385,260. Old offsets
remain unchanged. The bridge enumerates scripts once at injection using the
same bounded event-file reader as destination validation. The installed game's
catalogs contain 15, 36, 12, 24, 8 and 13 Arcade script scenes respectively;
these counts are observations, not hardcoded limits. Missing/invalid script
pointers are omitted. Tests include sparse IDs and the scene-255 boundary.

SELECT action 3 carries an explicit destination. It validates stage/scene ranges
and re-reads the actual destination scripts before queuing the existing native
mode/task/resource restart. It overrides startup selection and shares Load's
bootstrap, player joining, pause handling, progress, acknowledgment and timeout.
No INI selection key or additional save slot is introduced. Browsing never saves;
with Autosave off, a jump preserves the save. Normal Autosave can record the new
scene when enabled. Existing scene/route prerequisites remain experimental.

Full build/offline checks and WARP rendering pass. Menu/controller tests cover
root ordering, stage-to-scene navigation, held Trigger, native entrance defaults,
sparse IDs, pagination, Back, missing catalogs, read-only previews and serialized
requests. Native desktop smoke tests passed SELECT to chapter 1 scene 4 and
chapter 2 scene 0, rejected stage 20 and unavailable scene 255, and preserved the
save with Autosave off. Save/Load/New, pause and live Autosave regression checks
also passed. Both native runtime sessions were closed; no VR launch occurred.
Headset menu acceptance and full scene prerequisite coverage remain unverified.

## Keyboard control of VR Settings

F8 now uses the same edge/arming path as left-stick click, in desktop preview
and headset modes. Keyboard input is accepted only while the viewer or connected
native game's process has foreground focus. Arrows navigate/change values;
Enter/Right Ctrl confirm/back, matching native P1 bindings. Tab/Left Ctrl are P2
aliases; Backspace/Escape also back out. Keyboard Back closes at the root;
controller B/Y keeps its prior behavior. A simultaneous read-only companion
preview accepts keyboard input only with its own window focused.

VR combines keyboard and controllers before advancing menu edges once per frame,
including a keyboard fallback when action sync/tracking is unavailable. The
existing input heartbeat carries VC2_BTN_MENU while the panel owns input and
until closing keys release. The bridge masks those physical menu keys before
tracking native Start/Cancel; unrelated keys remain native. Input expiry releases
ownership through the existing 500 ms watchdog. No frame layout or INI changes.

Bridge/viewer builds and all offline/WARP checks passed. Added tests cover held
toggle/navigation/back, foreground gating, native P1/P2 key aliases, mixed VR
keyboard/controller input, failed action sync, immediate close-release publication,
native keyboard suppression and watchdog restoration. No game or VR session was
launched for that keyboard change; another session remained untouched. Native-window
rendering was added subsequently, as described below.

## Native-window settings panel

The viewer publishes the existing 640x480 straight-alpha menu canvas to an
independent sequence/heartbeat in protocol 23. Earlier offsets stay unchanged;
menu sequence starts at 87,385,260, pixels at 87,385,272, total frame size
88,614,072. Primary viewer only; companion previews never write the canvas.
Canvas updates and D3D11 texture updates have separate dirty state, so native
publication cannot consume either headset eye's texture upload. Unchanged menu
frames refresh only their heartbeat, not the 1.2 MB canvas.

The existing native presentation hook captures the game first, then starts a
separate D3D7 scene and composites a managed 32-bit texture as one alpha-blended
quad. No native menu geometry enters reconstruction or framebuffer capture.
The texture uploads only when its stable sequence changes, restores after loss,
and is recreated on device replacement. The compositor restores all changed
device state and viewport, then calls normal presentation even after failure.
Closed, torn or older-than-500-ms menu data is not drawn. Native pause replay
stays ahead of capture/composition. The game's EXE and assets are unchanged.

Native-only mode polls the same keyboard/settings path with desktop preview and
headset outputs both disabled. It still runs the viewer helper, without graphics
device or OpenXR initialization. Menu display/rendering rows retain their existing
effects on converted output; audio, cheats and saves use the same bridge actions.

Added real offscreen D3D7 checks cover alpha/color conversion, viewport and
texture/render-state restoration, native presentation order, capture exclusion,
stable/torn canvas sequences and closed/stale ownership. Viewer checks cover
publication, unchanged-frame reuse, separate texture dirtiness and read-only
preview guards. Full build/offline/WARP checks passed, including the real D3D7
compositor test. Live native-window/headset acceptance remains unverified; another
game session was active and was left untouched. Original EXE SHA256 is unchanged.


## Stage-select integration with current main

Merged the scene catalog/selection, native keyboard ownership and native-window
menu compositor with the reorganized settings and short per-option help. Resolved
the command-3 collision by retaining Return to Title as 3 and assigning Stage
select 4. Protocol 24 rejects both old main and protocol-23 feature binaries.
No protocol-23 canvas/catalog fields were reordered; no installed game files or
active runtime binaries are modified by the source merge. Combined live native
window and headset validation remains pending.


## Original Mode item selection receives native Fire edges

VR trigger edges reached the native gun state but not the separate native button
edge mask used by the Original Mode trunk menu. The bridge now also publishes
`4 << (player * 16)` for an accepted Fire edge, preserving existing native bits,
hold/release behavior, Grip-only reload and VR Settings input ownership. No
protocol or viewer change is needed. The earlier gun-visibility hypothesis was
disproved: P1 was active on this screen.

The isolated input regression failed before the bridge change and passes after
it, covering both player edges, held input and reload isolation. A desktop probe
through the normal Original Mode entry added Power Up 1.2, then Chamber +2, and
retained the two-item limit. Evidence is in
`build/original-item-input-candidate/` (native screenshots and input JSON).
Config and checkpoint bytes remained unchanged; the probe closed only its own
game. Headset acceptance remains pending. Training navigation needed no change.

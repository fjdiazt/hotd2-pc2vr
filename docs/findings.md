# Native integration findings — 2026-10-17

## Supported executable

`N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe`, x86, SHA256 `C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A`. MD5 matches DemulShooter's original PC release. The original EXE and `Hod2.ini` hashes were unchanged after desktop testing. `build\Hod2.ini.before-test` is the original configuration backup.

## Native seams

Addresses below are absolute for image base `0x400000`. Hooks use RVAs plus the running image base and validate original instructions.

- `0x49E461`: `push 0x0B` selects entry 11 of the native video-mode table at `0x5988F0`. Entry 7 is 640x480, 32-bit windowed. Changing only the immediate selects that existing mode; no external DirectDraw wrapper is needed. Windowed launch was observed on the desktop.
- `0x4A52FD` through `0x4A5340`: native device-selection loop asks a Yes/No question when multiple graphics candidates exist. `AllDevicePass` does not suppress this. An exact `MessageBoxA` prompt recognizer selects the native default; error and quit confirmation dialogs remain intact.
- `0x4A66E4`: native `WM_ACTIVATEAPP` handling. The guarded activation-branch patch preserves rendering when the VR viewer takes foreground focus.
- `0x4A59C0`, `0x4A5A00`, `0x4A5A40`: native XYZ triangle strips, transformed/lit strips, and XYZ triangle lists. These are the stable capture seams. Direct3D rewrites optimized draw-method slots after device setup, so hooking `DrawPrimitive` only at creation stopped capturing during gameplay. The copied 9-byte prologues contain no relative branches.
- `0x572304`: D3DX context `UpdateFrame` vtable slot, initially `0x4BA3FF`. This marks complete frames; publishing each `EndScene` emitted partial or empty frames.
- `0x40CBB0`: native gun-input update for both players and all selected control types. VR axes and trigger press edges are applied after native processing. Writing coordinates earlier allowed native recentering to erase them. The original keyboard aim function at `0x40CFE0` and its axis-write instructions at `0x40D1AD` and `0x40D1B7` remain unmodified.
- Player axes: P1 `0x9C8FC4/0x9C8FC6`, P2 `0x9C8FEC/0x9C8FEE`. Signed game coordinates are `x = screen_x - 320`, `y = 240 - screen_y`.
- P1 native gun state starts at `0x9C8FC0`, P2 at `+0x28`. Offset `0x10` marks valid aim; offset `0x14` is the shot press edge consumed by shooting code at `0x414C04`. The original keyboard-only trigger injection failed with P1's selected mouse control type (`0x9A5D85=1`), despite confirmed OpenXR haptics and IPC FIRE bits. Applying fire after native gun polling reaches the shared mouse/keyboard path. Held triggers produce one edge; release/repress produces another; physical shot edges are preserved.
- `DirectInputCreateEx` and keyboard `GetDeviceState` supply local reload/start/back keys. No global keyboard or mouse events are generated.

## Rendering

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

User confirmed scene/gun visibility, aiming, firing and improved appearance after material/blending correction. Atlas changes alone did not eliminate the reported white artifacts. Correct head tracking, stereo comfort, scale, reloading, HUD depth, player 2 joining, long-session texture-cache behavior and full-game coverage still need acceptance. Do not call the conversion finished before those checks.

Do not run the input self-check while someone is using the viewer. Do not kill unrelated VR services or other game processes. The native quit confirmation can block a graceful close; terminate only the explicitly identified HOTD2 test process when a rebuild requires it.

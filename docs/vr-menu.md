# VR settings menu

The viewer exposes the mod settings in a grouped, controller-operated panel.
Left stick click opens/closes it independently of native menus and pause.
Native Game Options remain unchanged. The background is 50% opaque; text and
selection accents retain their own coverage.

The supported executable SHA256 is
`C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A`.
The native entry points below were checked during the initial investigation.
The implemented menu extracts the game's glyphs into a private viewer texture;
it does not depend on native Options font residency or initialization.

## Native reuse

| Element | Evidence / boundary |
| --- | --- |
| Menu text | `0x487CA0(float x, float y, const char *text, unsigned flags)` uses the game's glyphs, white/red/grey styling and character-cell layout. Native Options uses normal `1` and selected `0x111` flags. |
| Font | Glyph map `0x56AED0` and sprite-to-file table `0x57A5BC` map letters/digits to texture file `0x15E`, `scr_opt_moji05.bin`. Ordinary punctuation is absent from this glyph map. Use compatible labels and numeric formats. |
| Resource lifetime | `Spr_Draw` at `0x41C630` requires its texture-file state to be loaded. Options initialization explicitly requests `0x15E`; common-resource reset does not keep it resident. Font availability must be managed for each relevant mode, including pause. |
| Main-menu entry | Mode-select task `0x496960` uses bitmap entries; its existing Options branch at `0x496B0F` requests native mode 12. Adding a row is more involved than extending a text list. |
| Existing Options | Mode 12 entry `0x4869E0`, cursor `0x486C10`, list `0x486DA0`, native row table `0x5696E0`. Initialization resets tasks/resources. Do not invoke the full Options mode while a stage is paused. |
| Pause | Native pause routine `0x40E500` and existing predicate hook `native_pause()` retain eligibility and ownership. Reuse those; do not set the pause flag directly. |
| Capture | `capture_draw()` rejects paused draws; `publish_frame()` retains the entire previous scene/HUD; `end_scene()` replays that scene on desktop. The viewer menu refreshes separately; the native menu composites after capture and before presentation. |

The local HOTD2RE reconstruction confirms the text API and Options layout in
`src/game/menu_text.*` and `src/game/world_modes_options.cpp`. Its port-only pause
menu is useful reference, not a drop-in native hook for this mod.

VC2VR's local `src/vc2vr.c:startupMenu()` groups settings into Cheats, Display,
Controls and Advanced. That implementation is a **console startup menu**;
its grouping and INI backup conventions are reusable, its presentation is not
an existing in-game VR menu.

## Menu design

Left stick click opens/closes VR Settings. The panel starts closed and preserves
native pause state. A/X retains native Start/Pause while the panel stays open;
Trigger selects, stick directions navigate/change values and B/Y returns to groups.
Right click recenters. F8 toggles the panel in the native window, headset or desktop preview, with either the native game or viewer focused. Arrows navigate/change values; Enter/Right Ctrl confirm/back, with native P2 Tab/Left Ctrl and Backspace/Escape aliases. Keyboard Back closes from the root; controller B/Y keeps its existing root behavior. Menu-owned keyboard keys are masked in the bridge until closing keys release, with the existing 500 ms input watchdog releasing ownership if the viewer disconnects. Other applications do not control the panel. Native-only mode (`--no-window --no-vrwindow`) keeps the viewer helper active for input/settings; no preview or headset is required. Companion previews remain read-only and listen only while their own window has focus.

| Group | Existing settings |
| --- | --- |
| Game Settings | Skip startup logos and live Autosave. This is the first group after Stage select. |
| Display | Brightness, gamma, player-HUD swap, desktop/headset outputs, native windowed mode and initial game focus. |
| Rendering | World scale, units per metre, resolution, MSAA, Sharp HUD, native lighting, ambient light, mesh/world expansion, depth mode/bias and background rendering, in that order. |
| Audio | Master, music and effects volumes. |
| Controls | Akimbo, arcade gun colors, laser, crosshair, reload duration, haptics, gun scale and aiming reach, in that order. |
| Cheats | Infinite ammo/lives/continues, invincibility, rescue NPC protection, rapid fire and its rate. |

The option table follows existing parsing, ranges and shipped defaults.
Swap player HUD includes gameplay join prompts and credit indicators alongside ammo/lives;
the global attract-screen prompt retains its native placement.
Runtime-capable settings update existing viewer variables and the established render/audio/cheat
heartbeat. GPU/resource/output initialization and bridge-startup settings must
show **restart required** and save for the next appropriate restart. Do not
promise live application of settings whose resources are only created at startup.

Use thumbstick navigation, Trigger to select, B/Y to go back and A/X for
native Start/Pause. Opening presses must be released before becoming menu actions.
Consume menu input before it can confirm native rows, join/fire, reload or skip
behind the settings page. Left click toggles settings; right click recenters.

Save only user-edited settings to the active mod INI, with a backup before its
first change. Opening the menu must not save unrelated CLI overrides or enable
cheats. Native game files and saved settings remain outside this menu's writes.

## Mode checkpoints

Save game uses the active Arcade or Original mode. Load game opens a two-row mode chooser with a saved-location preview. Stage select opens the same mode choice, then chapter and scene lists; Back returns through those levels without starting anything. During gameplay the mode chooser starts on the active supported mode; at title or in other modes it starts on Arcade.

Original Load/Stage select goes through native item selection before entering the destination. The panel closes while that selection is active, including for externally issued CLI requests. Request timeouts pause during item selection. The mod stores only mode, chapter and scene, with independent Arcade/Original checkpoint files. Equipment persistence is deferred; Training and Boss keep their native selectors.

The executable accepts `--game-mode arcade|original` (default Arcade), `--save`, `--load`, and the existing chapter/scene flags. Commands against a running bridge reuse the same request and acknowledgement path and exit without taking over the renderer or control heartbeat. `--save` never starts a game.

Protocol 25 appends mode, phase, response/request mode, item-selection progress and per-mode save previews after the native menu pixels. Older field offsets and command numbers remain unchanged; bridge and viewer must still match protocol versions.

Protocol 26 appends `ambient_light_percent` (0–100, default 100) after those previews. **Rendering > Ambient light percent** updates it through the existing control heartbeat. The bridge scales only ambient contributions and latches the setting for each captured frame; it needs Native lighting, applies after resume while paused, and uses the standard menu save/reset path. Direct light, specular, emission, fog and unlit/prelit artwork retain their native values. See [lighting configuration](technical-reference.md#advanced-graphics).

## Validation

Initial UI validation checked native-font placement over live captured geometry
and during pause through the actual viewer shaders. The user confirmed its
appearance and operation in VR before settings wiring was added.

`HOTD2VR.exe` generates `menu-font.rgba` from the owned supported game on first run,
without scripts or Python. The asset is not distributed. The viewer uses its own
texture instead of calling native Options or changing native resource loading.

Checks cover every public option's coverage, validation, live/restart behavior,
save failures, opening without writes, input isolation, native pause passthrough,
viewer expiry and retained scene restoration. Keep cheats off for mapping/menu
tests unless a particular cheat is deliberately being tested. Build/offline
success does not establish headset readability or comfort.

## Settings wiring

The viewer presents 39 numeric/boolean options
from its effective launch configuration, grouped and paginated. Live options
update their existing variables/heartbeat; brightness/gamma swap their three
affected pixel shaders as one successful set. Resource/startup settings stage
their values without changing running initialization state and display the
appropriate viewer/game restart label.

**Controls > Arcade gun colors** is live and defaults off (`[vr] arcade_gun_colors=0`).
It uses the existing gun mesh with an untextured matte-plastic material, bypassing
textures and baked material colors. Geometry-derived face normals provide ambient
fill, directional shading and a broad, restrained highlight. Lighting stays in gun
local space, like the existing baked gun lighting. Base colors are P1/right hand
scarlet `#DE242B` and P2/left hand cobalt `#1D4B8F`.
These are photo-based approximations of Sega shells
[2535-5408](https://www.gameroomguys.com/Gun-Cover-Set-Red) and
[2535-5407](https://www.gameroomguys.com/Gun-Cover-Set-Blue-Sega), not measured factory paint values.
Both gun appearances apply the same Brightness/Gamma output transform as the world,
after shading. Arcade plastic has 25% ambient fill, 65% directional contribution
and a restrained 1.2% highlight. Lasers and crosshairs share their player's shell
palette: scarlet for P1, cobalt for P2, whether this option is off or on. Firing
raises intensity while keeping that color. Turning the option off restores the textured/shaded material without
reloading the model. Neither material samples level lighting or shadows.
The flag also works with untextured/custom meshes and independent player joining.
Offscreen D3D11 checks cover UNORM and sRGB targets, both player colors, face
contrast, textured/untextured output, texture unbinding, live brightness/gamma,
restoration, INI loading, persistence and reset. Controller checks verify both players'
beam/crosshair colors at rest and while firing with the palette off and on. Headset color
matching remains unverified.

Close saves edited overrides only, including bridge keys in their own section, into
the active viewer INI. Values equal to their built-in defaults remove the key instead
of writing redundant defaults. The shipped INI contains no overrides; absent keys
match the documented defaults. Existing unedited keys are preserved. Same-directory staging, checked writes/disk flush and
atomic replacement preserve the current file and produce a timestamped backup.
An unchanged panel never writes; unrelated CLI overrides never become INI edits.
Save failure retains pending values and prevents an explicit close so
the user can retry. Normal viewer exit also attempts pending saves. Companion previews cannot
edit or save. Initial settings wiring added no native hooks or protocol changes; Save/Load integration below extends the checkpoint bridge protocol.

The user then requested an independent left-stick-click toggle instead of main
menu or pause integration. The panel starts closed. Left stick click (left
trackpad click on Vive) opens/closes it; desktop F8 does the same. Native pause
and resume never open/close the panel; stick-click toggles never pause/resume the game.
A/X retains native Start/Pause while the panel stays open or closed. B/Y backs
out to the group list without closing. Stick click or keyboard F8 toggles the VR panel.
The original native Options entry remains unchanged. Gun model selection stays in the INI;
diagnostic flags and the menu's own enable switch are not editable panel rows.
The launcher generates the native font before starting a game when absent.
Offline checks cover live values/heartbeat, real shader rendering, restart-only
staging, paging/edges/input isolation, unchanged/unknown INI keys and comments,
backup, locked-file retry, held stick clicks and unchanged native pause state.
Offline checks alone do not establish headset readability or comfort.

Live desktop validation changed master volume through the real menu, observed
its heartbeat update before any INI write, then verified its saved value and
backup on close. Brightness and world scale also saved their selected values.
All test values were returned through the menu; the active INI was restored
byte-for-byte. Cheat requests stayed zero and installed native files retained
their hashes. Final controller checks cover left-click edges and A/X native
pause passthrough for both hands while the settings panel stays open.

Return to Title follows Reset Defaults; Quit Game is the final root action. It saves pending
options, validates the native window against the connected game PID, and posts
WM_CLOSE. The existing quit-confirmation handler accepts the native prompt;
no additional hook or process termination is involved. A live desktop test
selected the new row and verified both native game and viewer exited normally,
with cheats off and an unedited mod INI unchanged.

Initial main integration retained protocol 18 and the independent player-HUD swap.
Swap player HUD is available live in Display and included in Reset Defaults.

## Save and Load integration

Root entries start with Save game, Load game and Stage select, followed by Game Settings,
Display, Rendering, Audio, Controls, Cheats, Reset Defaults, Return to Title and Quit Game. Root pages
eight entries; settings groups page six. Original-mode item-menu arrows replace page
counters: up when earlier rows exist, down when later rows exist. Game Settings
contains live Autosave (`continue_game`), persisted on close
with existing atomic INI backups. Opening settings reflects the effective startup
CLI value and never changes it.

The native startup extracts sprite `0x5F7` from the owned game's `tex/scr_item.bin`
into unused font slots 1/2, with a vertical flip for up. The `H2P2` cache replaces
older font caches atomically; extraction failure preserves the previous file.

Save works during active or paused Arcade/Original gameplay, independently of Autosave.
Load chooses a mode and also works from title/menu. Original opens native item selection before restarting its saved scene. Stage select
chooses an available stage/scene without deleting the save. Enabled Autosave can
later replace that slot. Existing players remain joined; menu loads start P1, or both
when akimbo is enabled. Controller/player assignments stay unchanged.

Progress captions resolve to acknowledged chapter/scene or an error and clear on
navigation. The viewer serializes and polls requests without blocking rendering.
Only a successful disk write reports Saved; only destination root/scene activation
reports Loaded. A 45-second viewer timeout reports an unknown result and retains
bridge ownership until acknowledgment. Companion previews cannot issue commands.

Protocol 20 appends an action and heartbeat-owned Autosave override to protocol
19's acknowledged save request. The bridge validates destination scripts against
bounded native event files before changing the game, then queues the native mode
transition for task/resource cleanup and reuses fresh Arcade/variant-0 setup.
Paused loads use the native pause predicate/routine, preserving its sound handling.
Initialization failure times out after 30 seconds, excluding Original item selection. This restarts a scene; actor,
script, player, route and exact moment state are not restored.

Native desktop smoke tests exercise paused Save/Load, cross-chapter Load, New game,
title/menu Load, live Autosave and invalid-scene rejection. They restore the old
save and leave personal INI bytes unchanged. Run explicitly after a build, with
no other game/viewer session running:

```powershell
python tests/test_checkpoint_runtime.py 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe'
python tests/test_checkpoint_modes_runtime.py 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe'
```

No VR launch occurs. Headset menu integration and full scene prerequisites still
need runtime confirmation.


Before selecting Save game, the caption shows the live current stage, chapter
name and scene, for example SAVE ARCADE STAGE 3 DARKNESS SCENE 7. The Load game mode chooser shows the
selected mode's saved destination instead. The bridge publishes both read-only locations under
the frame seqlock (protocol 21); inspecting captions never submits commands or
writes saves. No active supported game and missing/invalid-format save files get
explicit unavailable captions. Saved-file metadata refreshes at most once per
second and immediately after bridge saves, with disk reads outside the seqlock.
Current-location previews are suppressed during a pending native transition.

User confirmed both pre-selection destination captions in-headset at stage 3,
scene 11. The combined protocol-21 bridge also passed native desktop Save/Load,
Autosave, destination-metadata and scene-restart regression tests.

Scope and zoom controls, including HUD zoom, are absent from the public menu.
Their code and explicit INI overrides remain available; automatic zoom and scope
are disabled when keys are absent. Menu save/reset does not alter these hidden
overrides. Units per metre retains its game-restart requirement in Rendering.

## Return to Title

The root action above Quit Game saves pending settings, then sends the existing
bridge command channel a return-to-title request. It shares native transition
guards and acknowledgment with Load/New game, without reading or writing the save.
Native Game_SetState at 0x0040E850 writes the pending mode at 0x007C17A0;
Game_ApplyState at 0x0040E860 owns scene/task/resource teardown and player reset.
Mode 3 is the same destination as the launcher's Skip startup logos path. The
existing pause predicate resumes a paused run; success is acknowledged only after
mode 3 reaches sub-state 1, with no pending transition or native pause. The viewer
closes its panel on success and retains it on failure. The request cannot arm
checkpoint/CLI stage loading. Existing save data, Autosave preference and native
game files are preserved. Command 3 adds no shared-memory fields or native hooks;
older bridges reject it as unavailable. Both updated binaries are needed.

Offline tests cover deferred application, paused return, title initialization,
transition/signature readiness guards, Original mode, timeouts, unchanged saves,
preview and INI-save guards, and successful panel close. Native/headset acceptance
requires a fresh run; these checks do not establish the live transition.

World scale and Units per metre are adjacent at the start of Rendering. Sharp HUD
follows resolution/MSAA and defaults off in both the configuration loader and Reset
Defaults. Missing `hudsharp` uses 0; explicit overrides, including `hudsharp=1`, are
retained. Moving these options does not change their live/restart requirements.
Display contains seven rows on two pages; Rendering contains eleven and Controls eight, each on two pages.

Every setting carries a short explanation beside its existing label/key/defaults.
The footer displays that explanation, including **Akimbo: PLAY WITH TWO GUNS**;
root groups have short category descriptions, while Save/Load retain their location
previews. Generic APPLIES LIVE / SAVES ON CLOSE text is removed. Restart requirements,
paused-capture application notices, errors, preview restrictions and action results
use a separate line beneath the explanation. Existing native glyphs and the same panel
are reused. Offline checks exercise every setting's rendered help, width, row order,
both restart types, paused capture, errors, action status and read-only preview.


## Stage select

Stage select immediately follows Load game. Choose Arcade or Original, then one of six named stages,
then a native scene/checkpoint ID, then Trigger/Enter to start that scene.
Stage 3 initially highlights scene 7; other stages highlight their entrance
when present. Lists retain native zero-based scene IDs and page six rows.
B/Backspace returns from scenes to stages, then modes, then the root without changing
the game. Browsing also works in read-only previews; starting does not.

The bridge enumerates available Arcade scripts from bounded, relocated event
files once at injection. No scene counts are hardcoded. Invalid script entries
are omitted, missing stage data shows No available scenes. Confirmation uses
the existing task/resource restart with a fresh disk validation; changed or
invalid targets are rejected before unloading the current scene. Acknowledgment
and progress/error captions follow Load game. Autosave off preserves the current
save slot; Autosave on records the destination during normal gameplay.

Protocol 22 appends destination stage/scene and six 256-bit scene catalogs.
The scene list describes script availability, not proof of earlier script/route
prerequisites. Native desktop selection and invalid-target tests pass; headset selection still needs confirmation.


## Combined main integration

Stage select follows Load game, ahead of Game Settings. The current
Controls order, Akimbo label, per-option help, Return to Title, HUD ownership and
rescue-NPC protection remain intact. Stage select uses command 4; Return to Title
retains command 3. Matching protocol-26 bridge/viewer builds are mandatory because
older protocol-23 feature builds used command 3 for Stage select. The appended
scene catalogs and native menu canvas retain their protocol-23 field layout.

F8 and native keyboard navigation work with the original game window as well as
the viewer. Native input is masked only while the panel owns it, with watchdog
release. The viewer publishes the same menu canvas for native D3D7 composition
and headset upload; each tracks dirty state separately. The bridge skips stale,
closed or torn canvases and restores D3D7 state after alpha composition.

Integration checks are offline; fresh native-window and headset acceptance of
the combined build remain pending.

The main branch's graphics-free services mode remains unchanged: disabling both
outputs (or using NoGraphicsHooks) disables the menu and installs no graphics hooks.
Native-window menu composition requires capture enabled by a desktop/headset output.
The standalone native-window comparison launcher remains uninjected.

The root New Game action is removed. Stage select supplies explicit scene starts,
while Return to Title ends the current run. The legacy NEW bridge command remains
for protocol compatibility and existing native smoke tests; the VR menu cannot
submit it. This menu-only change does not alter protocol 24, INI values or saves.

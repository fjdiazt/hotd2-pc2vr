# Native VR settings menu: feasibility

Investigation branch: `feat/vr-menu`, based on `main` at `9d0f23d`.
Initial investigation changed no product code, launched no game and edited no
installed configuration.

Goal: expose the mod's existing options in a grouped, controller-operated menu,
available from the native main menu and reused while native gameplay is paused.
Preserve native game options, pause ownership and the retained 3D scene.

**Feasible in principle. This requires a menu feature, not another button remap.**
The supported executable's SHA256 is
`C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A`.
The entry points below were checked in that executable's disassembly. Runtime
font loading and live rendering while paused remain unproven.

## Native reuse

| Element | Evidence / boundary |
| --- | --- |
| Menu text | `0x487CA0(float x, float y, const char *text, unsigned flags)` uses the game's glyphs, white/red/grey styling and character-cell layout. Native Options uses normal `1` and selected `0x111` flags. |
| Font | Glyph map `0x56AED0` and sprite-to-file table `0x57A5BC` map letters/digits to texture file `0x15E`, `scr_opt_moji05.bin`. Ordinary punctuation is absent from this glyph map. Use compatible labels and numeric formats. |
| Resource lifetime | `Spr_Draw` at `0x41C630` requires its texture-file state to be loaded. Options initialization explicitly requests `0x15E`; common-resource reset does not keep it resident. Font availability must be managed for each relevant mode, including pause. |
| Main-menu entry | Mode-select task `0x496960` uses bitmap entries; its existing Options branch at `0x496B0F` requests native mode 12. Adding a row is more involved than extending a text list. |
| Existing Options | Mode 12 entry `0x4869E0`, cursor `0x486C10`, list `0x486DA0`, native row table `0x5696E0`. Initialization resets tasks/resources. Do not invoke the full Options mode while a stage is paused. |
| Pause | Native pause routine `0x40E500` and existing predicate hook `native_pause()` retain eligibility and ownership. Reuse those; do not set the pause flag directly. |
| Capture | `capture_draw()` rejects paused draws; `publish_frame()` retains the entire previous scene/HUD; `end_scene()` replays that scene on desktop. A changing menu needs a separately refreshed overlay above the retained scene, in both VR and desktop replay. |

The local HOTD2RE reconstruction confirms the text API and Options layout in
`src/game/menu_text.*` and `src/game/world_modes_options.cpp`. Its port-only pause
menu is useful reference, not a drop-in native hook for this mod.

VC2VR's local `src/vc2vr.c:startupMenu()` groups settings into Cheats, Display,
Controls and Advanced. That implementation is a **console startup menu**;
its grouping and INI backup conventions are reusable, its presentation is not
an existing in-game VR menu.

## Recommended design

Use the native main menu's existing Options entry as the integration point.
Offer **Game Options** and **VR Settings**, keeping the original game settings
reachable. Reuse one VR Settings page model from that entry and native pause.
Keep paused gameplay frozen while the menu changes independently.

| Group | Existing settings |
| --- | --- |
| Display | World scale, brightness, gamma, laser/crosshair, HUD sharpness and HUD zoom. |
| Rendering | Native lighting, mesh/world expansion, depth settings, MSAA and supersampling. |
| Audio | Master, music and effects volumes. |
| Controls | Akimbo, haptics, focus, gun/reload presentation, aiming reach, automatic zoom and scope controls. |
| Cheats | Infinite ammo/lives/continues, invincibility, rapid fire and its rate. |
| Advanced / startup | Desktop/headset outputs and bridge startup/window/background/unit settings. |

Inventory public launch flags and INI keys before implementing the option table.
Keep existing parsing, ranges and defaults authoritative. Runtime-capable settings
should update existing viewer variables and the established render/audio/cheat
heartbeat. GPU/resource/output initialization and bridge-startup settings must
show **restart required** and save for the next appropriate restart. Do not
promise live application of settings whose resources are only created at startup.

Use thumbstick navigation, Trigger to select, B/Y to go back and A/X to resume
the native pause. Opening presses must be released before becoming menu actions.
Consume menu input before it can confirm native rows, join/fire, reload or skip
behind the settings page. Keep left stick click reserved and right click recenter.

Save only user-edited settings to the active mod INI, with a backup before its
first change. Opening the menu must not save unrelated CLI overrides or enable
cheats. Native game files and saved settings remain outside this menu's writes.

## First proof and acceptance

Before building the full option catalogue, prove a native-font test page at the
main menu and during native pause. Check resource ownership, bounded glyph
capture, desktop draw ordering, VR visibility and unchanged retained-world data.
This resolves the two main unknowns: font lifetime and independently changing
pause UI.

Then check every public option's coverage, validation, live/restart behavior,
save failures, opening without writes, input isolation, per-player pause/resume,
viewer expiry and retained scene restoration. Keep cheats off for mapping/menu
tests unless a particular cheat is deliberately being tested. Build/offline
success does not establish headset readability or comfort.

## UI-only POC (superseded)

Uncommitted POC: launch the worktree viewer with `--menu-poc`. It uses glyphs
extracted from the owned game's files by `tools/poc_menu_font.py`, rather than
calling native Options or changing native resource loading. Group navigation and
sample values work; no real settings or cheats are changed or saved. In desktop
preview use arrows, Enter, Backspace and F8 to reopen. In VR use stick, Trigger,
B/Y and A/X; pausing opens the preview again. Left click remains reserved.

Cheap model/input smoke checks pass. A read-only offscreen render through the
actual viewer shaders places the menu over live captured game geometry; images
are in `build/menu-poc-integrated*.bmp`. Native font lifetime and headset comfort
remain unproven. Existing unrelated VR tests were left running; the POC was
started separately with `--preview-only --menu-poc` for desktop review.

## Settings wiring

The user confirmed the POC's appearance and operation in VR, then authorized
real option wiring. The viewer now presents 46 existing numeric/boolean options
from its effective launch configuration, grouped and paginated. Live options
update their existing variables/heartbeat; brightness/gamma swap their three
affected pixel shaders as one successful set. Resource/startup settings stage
their values without changing running initialization state and display the
appropriate viewer/game restart label.

Close saves edited keys only, including bridge keys in their own section, into
the active viewer INI. Same-directory staging, checked writes/disk flush and
atomic replacement preserve the current file and produce a timestamped backup.
An unchanged panel never writes; unrelated CLI overrides never become INI edits.
Save failure retains pending values and prevents an explicit close so
the user can retry. Normal viewer exit also attempts pending saves. Companion previews cannot
edit or save. No new native hooks or shared-memory protocol changes were added.

The user then requested an independent left-stick-click toggle instead of main
menu or pause integration. The panel starts closed. Left stick click (left
trackpad click on Vive) opens/closes it; desktop F8 does the same. Native pause
and resume never open/close the panel; stick-click toggles never pause/resume the game.
A/X retains native Start/Pause while the panel stays open or closed. B/Y backs
out to the group list without closing. Only stick click toggles the VR panel.
The original native Options entry remains unchanged. Gun model selection stays in the INI;
diagnostic flags and the menu's own enable switch are not editable panel rows.
The launcher generates the native font before starting a game when absent.
Offline checks cover live values/heartbeat, real shader rendering, restart-only
staging, paging/edges/input isolation, unchanged/unknown INI keys and comments,
backup, locked-file retry, held stick clicks and unchanged native pause state. Real option changes still need
headset acceptance beyond the earlier UI-only POC acceptance.

Live desktop validation changed master volume through the real menu, observed
its heartbeat update before any INI write, then verified its saved value and
backup on close. Brightness and world scale also saved their selected values.
All test values were returned through the menu; the active INI was restored
byte-for-byte. Cheat requests stayed zero and installed native files retained
their hashes. Final controller checks cover left-click edges and A/X native
pause passthrough for both hands while the settings panel stays open.

Quit Game follows Reset Defaults as the final root action. It saves pending
options, validates the native window against the connected game PID, and posts
WM_CLOSE. The existing quit-confirmation handler accepts the native prompt;
no additional hook or process termination is involved. A live desktop test
selected the new row and verified both native game and viewer exited normally,
with cheats off and an unedited mod INI unchanged.

Main integration retains protocol 18 and the independent player-HUD swap.
Swap player HUD is available live in Display and included in Reset Defaults.

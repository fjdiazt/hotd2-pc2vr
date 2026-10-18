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
| Capture | `capture_draw()` rejects paused draws; `publish_frame()` retains the entire previous scene/HUD; `end_scene()` replays that scene on desktop. The viewer menu refreshes separately above the retained scene; native desktop replay does not render it. |

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
Right click recenters. Desktop F8 toggles the panel; companion previews are read-only.

| Group | Existing settings |
| --- | --- |
| Display | World scale, brightness, gamma, laser/crosshair, HUD sharpness, HUD zoom and player-HUD swap. |
| Rendering | Native lighting, mesh/world expansion, depth settings, MSAA and supersampling. |
| Audio | Master, music and effects volumes. |
| Controls | Akimbo, haptics, focus, gun/reload presentation, aiming reach, automatic zoom and scope controls. |
| Cheats | Infinite ammo/lives/continues, invincibility, rapid fire and its rate. |
| Advanced / startup | Desktop/headset outputs and bridge startup/window/background/unit settings. |

The option table follows existing parsing, ranges and shipped defaults.
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

## Validation

Initial UI validation checked native-font placement over live captured geometry
and during pause through the actual viewer shaders. The user confirmed its
appearance and operation in VR before settings wiring was added.

The launcher generates menu-font.rgba through tools/extract_menu_font.py from
the owned supported game. The asset is not distributed. The viewer uses its own
texture instead of calling native Options or changing native resource loading.

Checks cover every public option's coverage, validation, live/restart behavior,
save failures, opening without writes, input isolation, native pause passthrough,
viewer expiry and retained scene restoration. Keep cheats off for mapping/menu
tests unless a particular cheat is deliberately being tested. Build/offline
success does not establish headset readability or comfort.

## Settings wiring

The viewer presents 47 numeric/boolean options
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
edit or save. Initial settings wiring added no native hooks or protocol changes; Save/Load integration below extends the checkpoint bridge protocol.

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
backup, locked-file retry, held stick clicks and unchanged native pause state.
Offline checks alone do not establish headset readability or comfort.

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

Initial main integration retained protocol 18 and the independent player-HUD swap.
Swap player HUD is available live in Display and included in Reset Defaults.

## Save and Load integration

Root entries start with Save game, Load game and New game, followed by existing
settings groups, Reset Defaults and Quit Game. Root pages eight entries; settings
groups page six. Advanced adds live Autosave (`continue_game`), persisted on close
with existing atomic INI backups. Opening settings reflects the effective startup
CLI value and never changes it.

Save works during active or paused Arcade gameplay, independently of Autosave.
Load also works from title/menu and restarts the saved Arcade scene. New game
starts chapter 1, scene 0 without deleting the save. Enabled Autosave can later
replace that slot. Existing players remain joined; menu loads start P1, or both
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
Initialization failure times out after 30 seconds. This restarts a scene; actor,
script, player, route and exact moment state are not restored.

Native desktop smoke tests exercise paused Save/Load, cross-chapter Load, New game,
title/menu Load, live Autosave and invalid-scene rejection. They restore the old
save and leave personal INI bytes unchanged. Run explicitly after a build, with
no other game/viewer session running:

```powershell
python tests/test_checkpoint_runtime.py 'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe'
```

No VR launch occurs. Headset menu integration and full scene prerequisites still
need runtime confirmation.

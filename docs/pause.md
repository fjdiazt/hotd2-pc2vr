# Unobstructed VR pause

The supported game stores native pause state at `0x9A1A00`. Its pause routine at `0x40E500` draws black model `0x93E` and pause sprites. An owned desktop probe (PID 69540) filtered the backdrop and found **zero world vertices** in successive paused frames, with six UI vertices remaining. Removing the overlay alone therefore cannot preserve the 3D view.

The bridge now retains the last complete gameplay capture while native pause is active: geometry, HUD, projection and shot anchors remain available in shared memory. Paused native draws still execute, but are not captured or uploaded into the texture atlas. The last screen capture is also retained. Frame publication, native input and viewer cheat heartbeats continue, allowing normal unpause. Fresh scene capture resumes immediately when native pause clears.

Protocol 13 publishes pause state in `reserved[1]`, formerly unused background-layer metadata. The layout and size do not change. The viewer reads it under the existing frame seqlock and suppresses the captured-screen panel during pause, including manually forced panels. Head and controller poses continue through the normal render loop. A viewer attaching during an existing paused session can read the bridge's retained scene. No menu image is substituted when gameplay has not produced a valid scene yet.

Verification:

- The previous bridge fails the new paused-capture regression. Fake-COM tests cover retained vertices/lens/anchors/atlas generation, repeated paused publications, live player status, resume and a cold pause without a prior gameplay scene.
- Viewer snapshot tests cover coherent pause state and suppressing automatic/forced panels. Full standalone build/offline checks passed.
- Owned native test PID **29124** retained **29,481 world vertices** and 78 HUD vertices. All paused samples had the same scene hash while frame publication advanced; unpause produced a different scene hash and fresh geometry. Native pause and resume used the usual Start button. All five cheats were enabled; the owned game closed normally.
- Actual offscreen D3D11 replay of the retained capture produced a visible street scene from two translated viewpoints, with different rendered images. This verifies retained 3D geometry rather than a flat screenshot.

Evidence is retained under ignored `build/clean-pause-evidence/` in the main checkout: `pause-empty-scene.json`, `pause-probe.json`, `clean-pause-build.log`, `pause-viewer-checks.log`, `paused-retained.bin`, `paused-retained-atlas.bin` and `paused-viewpoints.png`. No VR session was launched for these checks; headset comfort and broader stage/menu coverage remain pending.

## Separate Start and Pause input

Protocol 14 introduced `VC2_BTN_PAUSE=128`; protocol 15 separates Reload from Skip/Cancel without changing the frame layout. Current bindings:

- Trigger: Fire and Start/confirm; existing tilted-gun reload gesture remains.
- A/X: Start/confirm and Pause/resume.
- B/Y: Skip/Cancel only.
- Grip: Reload only.
- Right thumbstick click: Recenter once per press.
- Left thumbstick click: Unmapped and reserved. Both sticks retain four-way navigation.

Index uses A/B on each hand. Vive uses menu for Start/Pause, grip for Reload and right trackpad click for Recenter. The simple-controller menu supplies Start/Pause.

The native pause-input predicate at `0x40E9C0` has one direct caller, the unchanged pause routine at `0x40E500`. Its signature-checked hook temporarily replaces synthetic Start bits with per-player Pause press edges, then restores native input before other game handlers run. Physical keyboard Start remains available. A/X supplies both commands; Trigger supplies no Pause command. Native pause eligibility, ownership, focus/device-disconnect handling and pause/resume effects remain unchanged. The bridge never writes the pause-state flag.

Normal and Original weapon reload reads at `0x414A9A` and `0x414DD0` now read a separate input word. Both original instruction signatures are checked before changing either operand. Each native input poll copies physical/native input into that word, excludes synthetic Cancel and adds keyboard-profile Reload presses there. Gun profiles retain their native offscreen-trigger reload path. Skip readers at `0x414AF7` and `0x414E0E`, menu consumers, reload routines and native eligibility checks remain unchanged. Physical Ctrl is preserved, including simultaneous Ctrl and VR Cancel. No menu classifier or direct ammo/event-state writes are used.

Paired Start still mirrors in akimbo; Pause remains per-player. Stale VR input releases overrides. Original game files remain unchanged; restart without this bridge to remove the in-memory input changes. Protocol 15 prevents older viewers from sending the former combined Reload/Cancel mapping.

Verification in the `feat/input-mapping` worktree:

- Initial Trigger/A/B mapping passed native desktop checks and user headset confirmation before these follow-up changes.
- The new Cancel-without-reload regression failed against the previous bridge. Updated checks cover separate Reload/Cancel words, physical Ctrl, per-player Pause edges, native input restoration, signature rejection before either reload operand changes, held inputs and watchdog expiry.
- Viewer checks cover A/X Start/Pause, Trigger Start/Fire, Grip Reload, B/Y Cancel, hidden-player joining, right-only Recenter, left-click reservation and existing stick navigation.

- Full build and offline checks passed (`build/reload-split-build.log`). Existing MSVC CRT warnings remain.
- Owned native desktop session PID **53068**, protocol 15: Trigger reached the mode menu; A/X confirmed the game. Grip did not skip the opening event; B/Y skipped it. Both players fired `6 -> 5`, B/Y left ammo at 5, and Grip reloaded `5 -> 6`. A/X paused/resumed for the owning player without held-button repeats; Trigger and the other player's A/X could not resume that pause. P1 used native gun control and P2 keyboard control, exercising both reload input routes.
- No cheats requested; both players' native lives/ammo flags were verified off. The owned game closed normally; original `Hod2.exe` and `Hod2.ini` hashes stayed unchanged. Evidence: ignored `build/reload-split-live.json`, `build/reload-split-live.log`, `build/reload-split-bridge.log` and `build/check_reload_split_live.py`.

Latest physical controller mapping, complete menu coverage and Original-mode item weapons still need runtime acceptance. Offline/native desktop results do not establish headset acceptance.

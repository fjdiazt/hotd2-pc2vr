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

Protocol 14 adds `VC2_BTN_PAUSE=128` without changing the frame layout. Trigger sends Fire and Start; A/X sends the existing native Reload/Cancel, including eligible cutscene skip; B/Y sends Pause. Index uses A/B on each hand; Vive uses trackpad click for Reload/Cancel and menu click for Pause. The simple-controller menu button pauses.

The native pause-input predicate at `0x40E9C0` has one direct caller, the unchanged pause routine at `0x40E500`. Its signature-checked hook temporarily replaces only the synthetic Start bits seen by that predicate with per-player Pause press edges, then restores the native input word before other game handlers run. Physical keyboard Start remains available, including simultaneous physical Start and VR trigger. Native pause eligibility, pause ownership, focus/device-disconnect handling and pause/resume side effects remain in the original routines. The bridge never writes the pause-state flag itself.

This separates inputs without menu/gameplay classification. Paired Start still mirrors in akimbo; Pause remains per-player. Stale VR input releases the override. Original game files remain unchanged; restart the game without this worktree's bridge to remove the hook.

Verification in the `feat/input-mapping` worktree:

- The viewer regression first failed because a hidden hand's trigger did not send Start, then passed with the new mapping. Controller checks cover both hands, hidden-player joining, independent Fire/Start, Reload/Cancel and Pause actions, and existing stick navigation. Bridge checks cover pause press edges, paired Start masking, physical keyboard Start, input-word restoration and watchdog expiry. The full build/offline suite passed; existing MSVC CRT deprecation warnings remain.
- Owned desktop session PID **89676** reached the title/mode menus through trigger Start, joined P1 and then P2 independently, and verified each player's ammo `6 -> 5 -> 6` through Trigger then Reload/Cancel. Trigger did not pause play or resume a paused game. B/Y paused/resumed, holding it did not repeat, an inactive player could not pause, and the other player could not resume the owner's pause.
- That session requested zero cheats. The game's native lives/ammo flags were initially enabled, so the test cleared those flags only in its owned process before checking firing/reload/pause. Both players' native flags were verified off. The game closed normally and original `Hod2.exe`/`Hod2.ini` hashes stayed unchanged. Evidence: ignored `build/input-mapping-live.json`, `build/input-mapping-live.log` and `build/input-mapping-build.log` in this worktree.

No headset session was launched. Physical controller bindings, complete menu coverage, eligible cutscene skip and full-game behavior still need runtime acceptance; the existing native Cancel route remains unchanged.

The user subsequently confirmed the initial Trigger/A/B mapping works in the headset. A follow-up binds grip directly to the same per-hand OpenXR Reload/Cancel action as A/X and moves recenter to thumbstick click (Vive trackpad click). Stick tilt retains native menu arrows, and holding click recenters only once until release. No additional native hook or protocol change is needed. Grip/click headset acceptance remains pending.

A/X is not also bound to Start: native calibration adjusts its value from Reload (`0x4885F1`) before processing Start to accept (`0x4884D2`). Sending both would adjust and accept together. Separating those actions by screen would require the special handling the user excluded.

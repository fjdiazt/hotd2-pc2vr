# Arcade and Original Checkpoints Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement task-by-task. Work inline; no delegation.

**Goal:** Save/load scene checkpoints and select chapters/scenes in Arcade and Original from the settings menu and executable CLI.

**Architecture:** Extend the existing checkpoint record and bridge command with game mode. Preserve the existing Arcade checkpoint filename and add an Original checkpoint file. Native game transitions initialise each mode; Original uses its normal item selection before the requested destination.

**Tech Stack:** C11, Win32, native HOTD2 entry hooks, existing shared-memory bridge and tests.

**Spec:** [Scope](#scope), reflecting the approved implementation scope.

## Scope

- CLI defaults to Arcade; `--game-mode original` selects Original.
- Menu Save uses the active mode. Load and Stage select choose mode first.
- Save/load persists mode, chapter and scene only. No equipment capture, restoration, extra slots, or full save states.
- Original starts through native item selection; keep its pending destination until selection finishes, then use native chapter/scene initialisation.
- Training and Boss retain their native selectors and cannot write checkpoints.
- Existing Arcade saves remain readable. No game executable or native save-file edits.
- Keep graphics-disabled launches free of graphics hooks. No unattended VR launch.

## Review Focus

- Invalid mode or destination must leave the running game and prior saves untouched.
- Original item selection must remain usable while a request is pending, with no user-selection timeout.
- A request must not report success until its requested mode and scene are active.
- CLI actions against a running bridge must not open another renderer or expire the existing viewer's controls.
- Autosave must not record Original's item-selection scene over the intended checkpoint.

### Task 1: Records and native transitions

**Files:** `src/checkpoint.h`, `src/bridge.c`, `src/hotd_share.h`, `tests/test_checkpoint.c`.

**Interfaces:** `HotdCheckpoint {stage,scene,mode}`; Arcade=0, Original=1. Add mode/result/waiting fields at the end of `Vc2Frame`, preserve existing field offsets, and bump the protocol. Existing checkpoint read/write helpers accept legacy Arcade records.

- [x] Add regression checks for legacy records, separate mode records, invalid modes, Original's native item-entry and post-selection destination, unchanged Training/Boss, and save preservation.
- [x] Extend file records and mode-aware path selection using existing atomic writes.
- [x] Extend native load/save/select and autosave through existing hooks; validate signatures and destination before mutation.
- [x] Run the checkpoint executable and existing native-hook tests.

### Task 2: Menu and executable CLI

**Files:** `src/vr_menu.h`, `src/hotd2vr.c`, `src/launch.c`, `tests/test_vr_menu.c`, `tests/test_startup.c`, `tests/test_output_modes.c`, `tests/test_native_startup.py`.

**Interfaces:** `--game-mode arcade|original`, `--save`, `--load`, existing `--start-stage` and `--start-scene`. Share the menu's command publication/acknowledgement path. Save requires a running game; existing-bridge CLI actions exit after their command without taking ownership of cheats or renderer input.

- [x] Add checks for Arcade defaults, invalid/conflicting arguments, mode forwarding, mode menu/back navigation, save previews, and Original selection input.
- [x] Add mode choice to Load and Stage select, preselecting the active supported mode, otherwise Arcade.
- [x] Add CLI actions and startup mode forwarding; handle Original item-selection progress without creating a second viewer.
- [x] Run viewer/menu/output and launcher tests.

### Task 3: Verification and documentation

**Files:** `README.md`, `docs/vr-menu.md`, affected runtime/diagnostic protocol consumers.

- [x] Update protocol consumers, CLI examples, checkpoint filenames and the equipment limitation.
- [x] Run the full build and relevant Python suites.
- [x] Run bounded native-only checks with isolated settings/saves; close owned processes and verify the game executable hash.
- [x] Review final diff, commit and push to the verified private repository.

## Verification notes

- Full build/offline checks passed, including legacy save parsing, mode isolation, Original item entry, chapter-one gameplay flags, shared command publication, menu navigation and CLI control ownership.
- Python native-startup (11), packaging (6), launcher (2) and bat-diagnostic parsing (1) checks passed.
- Native-only runtime covered Arcade/Original Save/Load, cross-mode switching, paused Original Load, Original chapter-one scene 4 and chapter-two entry, default-mode rejection and invalid destinations. Evidence: ignored `build/mode-runtime-3lnb_ant/`.
- Native tests used external renderer-dialog confirmation and native keyboard presses. Graphics hooks and framebuffer readback remained disabled; owned processes closed and native game/save bytes were preserved.
- Ruling: keep the existing Arcade checkpoint filename and add an Original filename; legacy saves need no migration or rename.
- Ruling: do not restore equipment. Original enters native item selection and uses native chapter transition after selection; chapter-one entry clears the native item-selection flags once items have already been chosen.
- Headset operation and broader chapter/scene prerequisites remain unverified.

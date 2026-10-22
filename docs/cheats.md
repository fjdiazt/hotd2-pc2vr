# Native cheat implementation and checks

These addresses apply to the executable hash documented in the README. Unsupported signatures leave the affected feature unavailable.

## Invincibility

The native damage functions at `0x415300` (three cdecl arguments) and `0x415430` (five cdecl arguments) decrement lives and queue player hit events. The existing native infinite-lives byte skips the deduction inside those functions, but execution still reaches the hit-event and damage-cooldown writes. Hit events feed the camera-shake routine at `0x415270`.

Invincibility detours both entries before that work. Their first five bytes are complete, non-relative instructions: `56 8B 74 24 08` (`push esi; mov esi,[esp+8]`). Both entries and the existing native cheat signatures are checked before installation. Each trampoline preserves those instructions and returns to the original body. Damage passes through when the option is off, the heartbeat is at least 500 ms old, or installation did not complete. Enemy AI, attack animations and NPC health are untouched.

## Infinite continues

The native free-play DWORD at `0x9C8E70` controls credit admission. With value `1`, shared-pool admission at `0x406E10`, per-player admission at `0x406E70`, and availability at `0x406ED0` accept play without deducting credits. The bridge validates the corresponding reads/comparisons before owning this value.

The first fresh viewer request saves the original DWORD, including when infinite continues is off. Fresh requests force free play to `1` when enabled or `0` when disabled; viewer exit and heartbeat expiry restore the saved value. Lives/ammo use the same explicit override semantics for both players. Toggling settings during one connection does not replace the saved baseline. The mod does not refill credit pools, change lives, synthesize Start presses or alter the continue countdown.

## Rescue NPC protection

`protect_rescue_npcs=1`, launcher `-ProtectRescueNpcs`, viewer `--protect-rescue-npcs`, or **Cheats > Protect rescue NPCs** enables this default-off option. It shares the existing cheat heartbeat and changes no shared-memory layout.

Native `Civ_Update` at `0x48A920` handles ordinary civilians and is also called by both branches of vehicle update `0x48B140`. The hook validates its five-byte entry and the separate shot/death tests before installation. For those two civilian task identities only, a fresh enabled request clears pending player-hit bits `0xE` at actor offset `0x34` before running the entire original update. Enemy/script death bit `0x4000000` and all other actor data remain intact. Native enemy hit handling, shot collision/occlusion and player damage routines are not replaced. Already-dead civilians are not revived.

The entry `83 EC 18 53 56` contains complete non-relative instructions. The shot guard at `0x48AB8F` tests bit `8`; the preceding check at `0x48AADC` reads actor `+0x34` and tests enemy/script death independently. The native flow reaches the player life/score penalty and alternate death script only after that shot test. Disabling protection or expiring the 500 ms heartbeat passes subsequent pending hits through unchanged. No native game files are patched on disk.

Offline checks execute the entry-prologue trampoline, reject every mutated signature byte before patching, and exercise both civilian identities, both players, retained enemy/script deaths, unrelated actor data, unexpected actor types, off/stale/unsupported requests and unaffected player damage. Viewer checks cover default-off parsing, CLI/heartbeat delivery, menu persistence/reset and PowerShell forwarding. These establish hook dispatch and configuration behavior; native gameplay, rescue scoring/routes, Original-mode weapons and headset acceptance remain unverified.

## Verification

- `tests/test_protection.c` executes real five- and nine-byte trampoline probes; verifies both damage dispatchers for both player indices; checks stale/unsupported pass-through, native free-play signatures, credit/life preservation, option combinations and restoration of prior free-play values.
- `tests/test_frame_snapshot.c` verifies default-off INI parsing, individual CLI flags, combined requests and delivery without gun ownership. The normal build runs both suites.
- Owned desktop test PID **64192** passed: both players retained three lives and zero native hit-event flags for 18 seconds (165 samples) with invincibility enabled. Disabling it allowed a natural enemy attack to reduce P1 to two lives. Native infinite-lives bytes were explicitly cleared in this owned test session to make damage observable.
- The same session forced a zero-life state to exercise normal continue handling: P1 continued twice with shared credits at zero; P2 continued with both per-player credit pools at zero. Each returned to three lives through native Start confirmation, with credit counts still zero. These were controlled deaths, not a full-game enemy-damage run.
- Disable and watchdog expiry restored free play from `1` to its original `0`. A real desktop viewer with all five CLI cheat flags activated the requests and restored native flags/free play on exit. The owned game closed normally.

The final combined desktop check (PID **60044**, protocol **12**) repeated these checks successfully. Its continue test waited for the native countdown task before sending Start; all three continues were accepted on the first press. An earlier combined attempt sent P2's press before confirming countdown readiness and did not continue; its report is retained with the evidence.

Current runtime evidence is under ignored `build/`: `protection-live.json`, `protection-live.log` and `protection-cli.log`. The full source-integration snapshot, build log and earlier reports are retained under `build/protection-integrate-20261018-034114/`. These captures record their exact protocol/layout; matching bridge and viewer builds are required. Installed `Hod2.exe` and `Hod2.ini` hashes were unchanged after the final check. Headset behavior, every boss/attack path and full-game compatibility remain unverified. No VR session was launched for these checks.

# Culling review

Reviewed the visibility expansion introduced by `f5eb6cf` against `7abacd3`, including native submission flow, capture provenance, queue limits and CPU work. User saw rear scenery in VR, with jerkiness and visual artifacts. The defects below are separate from that observation: none has yet been tied to the exact headset frame.

## Findings and repairs

Repairs are in `src/bridge.c` and `src/capture_visibility.h`; focused checks run through `tests/test_capture_visibility.c` and `tests/test_vertex_reuse.c`.

| Finding | Evidence and consequence | Repair and focused regression |
| --- | --- | --- |
| Stage tags identified values, not submissions | The original helper compared record bytes 8..115. An unrelated record with identical values inherited stage expansion even at another address. | Immediate ownership is scoped to the stage submission. Deferred ownership uses the actual allocated address plus all 116 post-copy bytes, consumed during pass 1. Tests cover equal-value aliases, changed sort keys, recycled addresses and real queue copies including ALT alpha rewriting. |
| Overflow revoked earlier tags | On the 65th unique record, the original helper set `overflow`; a deferred copy of the first accepted record then failed lookup. Immediate geometry could expand while its deferred pass fell back to native culling. | Saturation refuses new expansion while retaining accepted queued records. A regression fills the table, rejects another tag, and still expands the first record's deferred draw. |
| Native deferred queues lacked a capacity guard | Initialization at `0x4ABEA1..0x4ABEAB` supplies 1,024 sort-pointer entries. Normal/ALT models and sprites append unchecked. The bridge's 64-tag limit did not protect shared native storage. | World-enabled capture validates queue metadata and room before forced appends across all three paths. When a normal model's immediate pass already drew, a blocked deferred append does not erase that draw. Tests cover exact capacity, malformed metadata, all append paths and preservation of native low-byte return semantics. |
| Disabled mode still did stage work | Submission wrappers read model/black-model data before checking the world flag; model draws could search tags with expansion disabled. | Disabled paths return before queue reads or stage lookup. A test marks native queue metadata PAGE_NOACCESS and runs normal/ALT/sprite submissions with world expansion off. |
| Overflow logging amplified overload | Every overflowing frame requested a diagnostic, and `logline` flushes synchronously. | Log pressure transitions at least 60 frames apart, retaining the 300-frame cadence. A 1,000-frame steady-pressure run plus recovery produces five lines; 1,000 alternating-pressure frames produce at most 20. |
| Strip vertices repeated lighting work | A 100-vertex strip emits 98 triangles and previously converted/lit 294 corners. This predates world expansion; added geometry amplifies it. | A draw-local three-entry cache reuses strip/fan conversion results; unindexed triangle lists bypass it. Strip and fan examples require 100 lighting evaluations, preserving captured bytes and triangle order. |

Visibility flags now latch at the frame boundary, so a midframe flag update cannot split immediate and deferred visibility policy; a regression changes flags between those passes. Native visibility calls still run, preserving engine matrix side effects.

Native queue metadata: capacity `0x7E78C8`, count `0x7E793C`, sort list `0x7E78C4`, record cursor `0x7E78C0`. Guards apply while experimental world expansion is enabled; within-budget submissions retain their native execution. Full queues reject further deferred writes rather than allowing native storage corruption.

The production vertex-reuse check confirms identical captured bytes and drop counts across a 41,976-byte fixture (SHA256 `875efc46e3be37f6364f87b1c36b5cbdba6fab0c45a7c22fde685c23646c01a6`). Cases include indexed/degenerate input, bad indices, nonfinite vertices, partial conversion failure, HUD/material/provenance changes and uncached unindexed lists. Null/zero/invalid-FVF draws are rejected before vertex inspection. The 294-to-100 result measures work count, not frame time.

Full build and all offline/GPU suites pass (`build/culling-review-build.log`). Independent review found no remaining defects, including deferred sort-list lifetime and native AL low-byte return handling. Headset acceptance requires a later authorized test.

## What the runtime evidence establishes

`build/capture-expansion-live.json` sampled an unpaused opening street: default capture had roughly 29.7k world vertices at 49 FPS; expanded world roughly 34.8k at 42 FPS. Repeated samples showed the same direction, with no dropped triangles. Actors and time were not frozen, so this does not isolate the cost of any individual hook or lighting calculation.

The subsequent headset session, `build/world-expansion-vr.log` (game 91376, viewer 85256), used mesh/world expansion, brightness 80%, all convenience cheats and rapid fire at 10 shots/sec. User confirmed rear scenery, but reported jerkiness and artifacts. Logged stage tags were typically 3..7 with no overflow, so tag saturation and overflow logging do not explain that observed run. Linear scans are bounded at 64; a hash/cache redesign has no measured justification here.

After repair, an initial native-window session (PID 60624) passed seven flag sweeps but included intro frames. A fresh owned session (PID 17560) waited 20 seconds before sampling gameplay; `build/culling-review-live.json` records seven sweeps with nonzero geometry and zero drops. Defaults captured roughly 29.5k world / 4.3k rear vertices; repeated world expansion roughly 34.8k / 9.6k. Hooks/policy were ready, with no sampled queue drops, stage-tag overflow or texture failures. The unpaused moving scene ran around 54 FPS default versus 47 FPS expanded; this is not a controlled performance comparison or proof of speedup from the fixes.

The owned native game closed normally; no game/viewer remains running. The installed executable retained SHA256 `C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A`. No VR session was launched for these repairs.

## Current defaults and remaining acceptance

- `expand_world=0`: keep experimental rear capture off.
- `expand_meshes=1`: retain the existing normal-model side expansion.
- `brightness=90`: future playtests use the configured default, without a brightness override unless requested.

Full rear coverage, forward-wall parity, section transitions and headset performance remain unqualified. The changes do not cache unloaded scenery or create missing surfaces. No VR launch is authorized by this review; further headset acceptance waits for the user's test request.

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

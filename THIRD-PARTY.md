# Third-party components

- Viewer renderer, OpenXR/controller/gun code, and `assets/gun/gun.obj`, `gun.mtl`, `gun.bmp`: NeuralF's [Rea-Virtua-Cop-2-VR](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR), copied from the local fork at commit `e3189a5b020d71df2597243d184786abe617d889`. MIT license retained in `LICENSE`. HOTD2-specific integration replaces VC2 launch, memory, input and renderer hooks.
- OpenXR headers: Khronos OpenXR SDK, API header version 1.1.62. Copyright and SPDX notices are retained; license at `third_party/openxr/LICENSE`.
- `openxr_loader.dll`, when copied from the existing VC2VR installation, is Khronos's OpenXR loader. It is a runtime build dependency, not game code.
- HOTD2 input locations and keyboard conventions were cross-checked against [DemulShooter's HOTD2 PC support](https://github.com/argonlefou/DemulShooter/blob/master/DemulShooter/Games/Game_WndHod2pc.cs), then checked against the installed executable's instructions. No DemulShooter code is included.

The installed game and captured game imagery remain the user's own copy and are not part of the source distribution.

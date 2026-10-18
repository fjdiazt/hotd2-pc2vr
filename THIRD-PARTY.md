# Third-party components and asset provenance

## VC2VR foundation

HOTD2 PC2VR adapts substantial viewer, Direct3D11, shared-memory, OpenXR, controller, sight and gun-rendering code from **[NeuralF's Rea-Virtua-Cop-2-VR (VC2VR)](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR)**.

The initial import came from the local fork at commit `e3189a5b020d71df2597243d184786abe617d889`. The upstream project is published under the [MIT license](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR/blob/d99da1e6795ead74ac1bd9f24eec1a89e414ca62/LICENSE); NeuralF's copyright and permission notice are retained in this repository's [LICENSE](LICENSE). Source-file adaptation notices remain in place.

HOTD2-specific work replaces the VC2 launcher, renderer interception, memory locations and native input integration. VC2VR's pluggable-renderer installation and extra rotated-camera passes are not part of this HOTD2 implementation.

## Gun model and texture

The following assets are reused from [VC2VR's gun directory](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR/tree/d99da1e6795ead74ac1bd9f24eec1a89e414ca62/gun):

| Local file | Upstream file |
| --- | --- |
| `assets/gun/gun.obj` | `gun/gun.obj` |
| `assets/gun/gun.mtl` | `gun/gun.mtl` |
| `assets/gun/gun.bmp` | `gun/gun.bmp` |

The [OBJ header](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR/blob/d99da1e6795ead74ac1bd9f24eec1a89e414ca62/gun/gun.obj) identifies the asset as:

> Emerald revolver (Meshy), reoriented for VC2VR:
> metres, muzzle down -Z, bore axis through the origin.

The material is named `emerald` and uses `gun.bmp`. HOTD2 uses this supplied asset for both controller guns, with the viewer's gun scaling and animation.

Upstream provenance was checked on **2026-10-18**, at commit `d99da1e6795ead74ac1bd9f24eec1a89e414ca62`. The OBJ and MTL contents match after CRLF/LF normalization; the BMP matches byte for byte. The checked upstream README, license, repository tree and gun files provide no separate artist name, original model-page URL or dedicated asset-license file. “Meshy” is the attribution label found in the model header; an individual model author has not been verified. The upstream repository's MIT notice is retained above without inventing a more specific artist credit.

## OpenXR

- **Headers:** [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK), API header version 1.1.62. Copyright and SPDX notices remain in the headers (`Apache-2.0 OR MIT`); the supplied license text is retained at [third_party/openxr/LICENSE](third_party/openxr/LICENSE).
- **Loader:** `openxr_loader.dll`, when copied by the build from the existing VC2VR installation, is Khronos's x64 OpenXR loader. It is a runtime dependency, separate from either game's code. The build preserves an existing local loader.

## Input references

HOTD2 input locations and keyboard conventions were cross-checked against [DemulShooter's HOTD2 PC support](https://github.com/argonlefou/DemulShooter/blob/master/DemulShooter/Games/Game_WndHod2pc.cs), then verified against instructions in the supported installed executable. No DemulShooter code is included.

## Game ownership

The installed HOTD2 executable, game data and captured game imagery remain the user's own copy and are not included in this source distribution. No Virtua Cop 2 game files are included. The bundled revolver assets are the separate VC2VR assets described above.

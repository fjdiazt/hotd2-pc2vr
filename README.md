# HOTD2 PC2VR

Play the original Windows version of **The House of the Dead 2** in VR. Look around the scene and aim with your motion controllers as light guns. Save and load scene checkpoints, choose your starting stage and scene, and enable optional cheats.

![The House of the Dead 2 VR gameplay](docs/screenshots/gameplay-vr-animated.png)

## How to play

You need your own installed Windows PC copy of the game, Windows 64-bit, a Direct3D 11-capable graphics card, a PC VR headset, and motion controllers. The Remake and emulated versions are not supported.

1. **Get the mod.** Download the Windows ZIP from [Releases](https://github.com/fjdiazt/hotd2-pc2vr/releases). If no ZIP is available, see [Build from source](docs/technical-reference.md#build-from-source).
2. **Install.** Extract the ZIP into the folder containing your game's **`Hod2.exe`**. It includes a **`vr`** folder.
3. **Connect your headset and controllers.** Start your usual PC VR software and make sure the headset is ready.
4. **Start the mod.** Open `vr` and double-click **`start.bat`** to launch **`HOTD2VR.exe`**. It finds and starts the game automatically.
5. **Center the view.** Face forward and press the **right thumbstick button**.
6. **Start playing.** Choose a game mode with a thumbstick, then pull the **trigger** to confirm.

### Buttons you need

| What you want to do | What to press |
| --- | --- |
| Shoot | Pull that hand's **trigger**. |
| Reload | Press that hand's **grip**. You can also point the gun up, down or sideways away from your view and pull its trigger. |
| Navigate game menus | Move either **thumbstick**; pull the **trigger** to confirm. |
| Pause or resume | Press **A/X**. |
| Skip a scene or go back | Press **B/Y**. |
| Recenter | Face forward and press the **right thumbstick button**. |
| Open or close VR Settings | Press the **left thumbstick button**. |

The right controller is Player 1; the left is Player 2. By default, **Akimbo** is on, so starting joins both players. Turn it off in **VR Settings > Controls** if you want each hand to join separately.

For Vive controllers, use the menu button for Start/Pause and the left/right trackpad clicks for Settings/recentering. On Index, use A/B on either hand.

When finished, choose **Quit game** in VR Settings to close both the game and its VR viewer.

## VR Settings menu

Press the **left thumbstick button** to open VR Settings.

![VR Settings main page with Save game, Load game, Stage select and settings groups](docs/screenshots/vr-settings.png)

- **Up/down:** select an item or scroll to the next page.
- **Left/right:** change the selected setting.
- **Trigger:** open the selected group or activate the selected action.
- **B/Y:** go back.
- **Left thumbstick button:** close and save.

**Settings does not pause the game.** Press **A/X** to pause or resume.

On the desktop, use **F8**, **arrow keys**, **Enter** and **Right Ctrl** to open the menu, navigate, select and go back.

The footer explains the selected option and shows any restart requirement. To restart, choose **Quit game** and launch the mod again.

## Settings explained

The menu starts with **Save game**, **Load game** and **Stage select**, followed by **Game Settings**, **Display**, **Rendering**, **Audio**, **Controls** and **Cheats**. **Reset defaults**, **Return to title** and **Quit game** follow the groups.

Defaults below apply to a fresh installation. Toggle values appear as **ON/OFF**. Options apply immediately unless a restart is listed; lighting and visibility changes made while paused apply when gameplay resumes.

### Game actions

| Action | What it does |
| --- | --- |
| **Save game** | Saves the current Arcade or Original chapter and scene, including while paused. Each mode has its own checkpoint. Equipment and exact game state are not saved. |
| **Load game** | Choose Arcade or Original, then load that mode's checkpoint. Original asks you to choose equipment before restarting the saved scene. |
| **Stage select** | Choose Arcade or Original, then a chapter and scene. Original asks you to choose equipment first. Training and Boss use their normal stage selectors. |
| **Reset defaults** | Restores all menu defaults and turns cheats off. |
| **Return to title** | Ends the run and returns to the title screen. Saves settings and keeps the checkpoint. |
| **Quit game** | Saves settings and closes the game and VR viewer. |

### Game Settings

| Option | Default | What it does |
| --- | --- | --- |
| **Skip startup logos** | On | Skips the SEGA/Empire boot logos. Game restart required. |
| **Autosave** | Off | Saves scene progress separately for Arcade and Original, then resumes the matching mode on its next new start. Uses each mode's Save game slot. |

### Display

| Option | Default | What it does |
| --- | --- | --- |
| **Brightness percent** | 90% | Brightness of the scene and guns; 100% is neutral. Leaves the HUD unchanged. Range: 25–400%. |
| **Gamma percent** | 220% | Adjusts picture tones; 100% disables correction. Range: 0–300%. |
| **Swap player HUD** | On | Places Player 1's ammo, lives and join/credit row on the right, and Player 2's on the left. Off uses the original layout. |
| **Desktop preview** | Off | Opens a separate desktop view of the reconstructed scene. Game restart required. |
| **Headset output** | On | Sends the game to your headset. Game restart required. |
| **Native windowed** | On | Runs the original game in a desktop window. Game restart required. |
| **Focus game** | On | Brings the game window to the front at startup. Viewer restart required. |

### Rendering

| Option | Default | What it does |
| --- | --- | --- |
| **World scale percent** | 125% | World size: 100% is original scale; 125% is 25% larger. Range: 10–1000%. |
| **Units per metre** | 10 | Leave at 10; use World scale to change the world size. Game restart required. |
| **Resolution percent** | 100% | Headset image resolution. Higher is sharper but costs performance. Range: 50–250%. Viewer restart required. |
| **MSAA** | 4 | Smooths jagged edges. Choices: 1 (off), 2, 4 or 8. Viewer restart required. |
| **Sharp HUD** | Off | Makes HUD sprites crisper. |
| **Native lighting** | On | Uses the game's lighting, highlights and fog. |
| **Ambient light percent** | 100% | Reduces fill light to deepen shaded areas. Range: 0–100%; 100% keeps the original lighting. Requires Native lighting. |
| **Expand meshes** | On | Keeps character parts visible beyond the original screen edges. |
| **Expand world** | On | Captures more loaded scenery beside and behind you. Experimental; may add glitches and PC load. |
| **Depth mode** | 2 | Surface ordering: 0 uses draw order, 1 uses game sorting values, 2 uses 3D depth. Viewer restart required. |
| **Depth bias** | 0 | Separates overlapping surfaces to reduce flicker. Range: 0–200. Viewer restart required. |
| **Background rendering** | On | Keeps drawing when another app has focus. Game restart required. |

### Audio

Volume sliders range from 0–100%.

| Option | Default | What it does |
| --- | --- | --- |
| **Master volume** | 100% | Overall game volume; 0 mutes it. |
| **Music volume** | 100% | Background music, multiplied by Master volume. |
| **Effects volume** | 100% | Gunshots, voices and other effects, multiplied by Master volume. |

### Controls

| Option | Default | What it does |
| --- | --- | --- |
| **Akimbo** | On | Shows both guns and joins both players together. Off allows each hand to join separately. |
| **Arcade gun colors** | Off | Red gun for Player 1, blue for Player 2. Off uses the original gun textures. |
| **Laser** | On | Shows aiming beams. |
| **Crosshair** | On | Shows aiming markers. |
| **Reload duration ms** | 180 | Gun reload animation length; native weapon timing still applies. Range: 60–1000 ms. |
| **Haptics** | On | Controller vibration after confirmed shots. |
| **Gun scale percent** | 100% | Size of the gun models. Range: 10–1000%. Viewer restart required. |
| **Aim reach metres** | 6 | Aiming distance over empty space. Shots hitting scenery use the actual hit distance. Range: 1–100 metres. |

### Cheats

Cheats apply to both players.

| Option | Default | What it does |
| --- | --- | --- |
| **Infinite ammo** | Off | Stops ammunition being used. If the gun is already empty when enabled, reload once. |
| **Infinite lives** | Off | Prevents life loss and damage-related score/rank penalties. |
| **Invincibility** | Off | Blocks player damage, hit reactions and damage-related camera shake. Enemies still attack. |
| **Protect rescue NPCs** | Off | Prevents players from shooting rescue NPCs. Enemies and scripted events can still kill them. |
| **Infinite continues** | Off | Enables free play, so joining/continuing does not require credits. Confirm Continue after death. |
| **Rapid fire** | Off | Repeats shots while you hold the trigger. Reload gestures take priority. |
| **Shots per second** | 4 | Firing rate while Rapid fire is on. Range: 1–30; native weapon timing may limit it. |

## Saved progress

Save game and Autosave share one checkpoint per mode. Saving again replaces that mode's previous checkpoint. Turning Autosave off keeps both checkpoints and leaves manual Save/Load available.

Checkpoints restart a scene; they do not restore health, score, equipment, inventory, enemies or exact progress within the scene. Original asks you to choose equipment again. Some scenes may rely on earlier events and may not work correctly when loaded directly; try the chapter entrance instead.

To back up progress, copy `HOTD2VR.checkpoint.ini` (Arcade) and `HOTD2VR.original.checkpoint.ini` (Original) from `vr`. These are separate from the game's own saves.

## Troubleshooting

- **Unsupported executable:** only one original Windows game build is supported. See the [supported executable](docs/technical-reference.md#requirements).
- **Game does not start or reports a bridge mismatch:** close the game and viewer, then run `start.bat`. Keep the files from the same mod release together.
- **Headset unavailable:** check that your PC VR software is running, the headset is connected, and its OpenXR runtime is active.
- **Unexpected settings:** check `vr\HOTD2VR.ini`, or use **Reset defaults** in VR Settings.
- **Missing scenery when looking around:** some areas were never drawn by the original game. **Expand world** can reveal more loaded scenery, but may introduce glitches or reduce performance.

## Uninstall

Close the game and viewer, then remove the `vr` folder. Keep `HOTD2VR.ini` and both checkpoint files if you want to preserve settings and saved progress. The original game executable and settings stay unchanged.

## Technical documentation

[Technical reference](docs/technical-reference.md): command-line options, INI settings, build and release instructions, checkpoint protocol, architecture, and diagnostics.

Implementation notes: [VR menu](docs/vr-menu.md), [cheats](docs/cheats.md), [pause](docs/pause.md), and [investigation findings](docs/findings.md).

## Credits

Based on [NeuralF's Virtua Cop 2 VR (VC2VR)](https://github.com/NeuralF/Rea-Virtua-Cop-2-VR), adapted for HOTD2. Its MIT copyright notice is retained in [LICENSE](LICENSE).

- **Gun model and textures:** the Emerald revolver (Meshy), bundled with VC2VR. See [asset provenance](THIRD-PARTY.md#gun-model-and-texture).
- **[The Khronos Group](https://github.com/KhronosGroup/OpenXR-SDK):** OpenXR headers and loader.
- **[Argonlefou / DemulShooter](https://github.com/argonlefou/DemulShooter):** reference for HOTD2 controls. No DemulShooter code is included.

Full component and asset notes: [THIRD-PARTY.md](THIRD-PARTY.md). The House of the Dead 2 and its game imagery belong to their respective owners; this independent fan mod is not affiliated with or endorsed by Sega. No HOTD2 or Virtua Cop 2 game files are distributed here.

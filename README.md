# Frame Window Counter (Geode mod)

Automatic frame window counter for Geometry Dash 2.2081 / Geode 5.11.2. Every click and release you make is measured in **240 FPS frames** and shown as `0-1`, `1-2`, `2-3`, ... in an overlay, like the frame window breakdown videos, but live and automatic.

## Getting the .geode file

You need to compile the mod once. Pick one of these:

### Option A: GitHub Actions (no tools needed)
1. Create a new GitHub repository and upload everything in this folder (keep the `.github` folder).
2. Open the **Actions** tab. The "Build Geode Mod" workflow runs on every push (or press **Run workflow**).
3. When it finishes, download the **Build Output** artifact. It contains `nosyarg.frame-window-counter.geode`.
4. Put that file in your Geode mods folder (`Geometry Dash/geode/mods`) and restart the game.

### Option B: Build locally
1. Install the [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli) and run `geode sdk install` then `geode sdk install-binaries`.
2. Install CMake and a C++ compiler (Visual Studio 2022 on Windows).
3. In this folder run `geode build`. The `.geode` file is placed into your mods folder automatically.

The mod depends on `geode.node-ids` (used to put the button in the pause menu). Geode installs it automatically.

## Using it

- **Overlay** (top-left by default): your latest inputs, newest at the top.
  - `#12 Click   2-3   -1.25 / +0.75` means click 12 had a 2-3 frame window: it could have been 1.25 frames earlier or 0.75 frames later.
  - Colours: magenta 0-1, red 1-2, orange 2-3, yellow 3-4, green 4-6, cyan 6-10, grey 10+ / free.
  - `...` = still being measured (it needs about 0.75 s of the future trajectory), `free` = more leeway than the max window setting, `*` = you died before that input could be fully measured.
- **Death**: the counter resets. The overlay switches to a summary of the run you just finished until your next click.
- **Report**: pause, then press the green stats button on the right. It lists every input of the current and previous runs (arrows switch between runs) with a count per difficulty.
- **Move the overlay**: in the report press **Move Overlay**, drag the blue box, press **Done**. **Reset** puts it back in the corner.
- **Settings** (Geode mod settings): rows shown, scale, opacity, early/late display, max window, inner hitbox size, open report on death, history size.

## How the window is calculated

1. Every physics tick (240/s) the mod records player 1's position and hitbox; every click/release is stamped with its tick.
2. To test an input moved by `d` ticks, it rebuilds the trajectory: horizontal position comes from your real run (x movement doesn't depend on inputs); before the moved input the player keeps doing what it was doing; after it, the player follows the same vertical curve the real input produced, starting from the new point. After the next real input, the height difference carries over until the player lands.
3. That path is tested against spikes/hazards (full hitbox) and blocks (inner hitbox). Things your real run touched safely (the floor, blocks you slid on) are ignored.
4. It steps outwards from your real timing in 0.25-frame steps until the path dies. The surviving range is the window.

Tested against a brute-force simulation of a cube triple spike at 1x speed: true leeway 10 ticks, estimated 10.0 frames.

### Limitations
- It is a model of your trajectory, not a full re-run of the game. Orbs, pads, portals, moving objects, slopes and rotated hitboxes are approximate, so treat odd numbers on those as rough.
- Only player 1 is tracked; dual mode measures the top player only.
- Platformer levels and sideways (rotated) gameplay are not measured.
- Inputs that changed nothing (cube releases, mid-air clicks, buffered clicks) are hidden by default; turn on "List Inputs With No Effect" to see them.
- If you use a TPS bypass, set "Physics Ticks Per Second" to match.

## Files

```
mod.json                     mod metadata + settings
CMakeLists.txt               build script
src/main.cpp                 game hooks (ticks, inputs, death, reset, pause menu button)
src/Tracker.hpp/.cpp         recording + frame window calculation
src/Hud.hpp/.cpp             the in-game overlay
src/ReportPopup.hpp/.cpp     run report popup + drag-to-move overlay screen
about.md / changelog.md      shown on the Geode mod page
logo.png                     mod icon
.github/workflows/           GitHub Actions build for all platforms
```

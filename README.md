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

The same way you'd test it by hand with a macro bot, but automatically and live:

1. Every physics tick (240/s) the mod saves a copy of player 1.
2. When you click or release, it spawns invisible "ghost" players from those copies. Each one does your input a little earlier or later (half-frame steps by default), then runs on **Geometry Dash's own physics and collision code**, in step with you, through the obstacles ahead.
3. A ghost's result is final when it dies, when it ends up in exactly the same state as you, when you make your next input that matters, or when you go through a portal. That's why a window shows `...` until you've actually passed the obstacle it was for.
4. The surviving range of timings around your real one is the window, in 240 FPS frames. `1-2` means between 1 and 2 frames of room, like NanGD's videos (the exact frame count depends on alignment).

Every input that reaches the player counts: mouse, any key, controller or touch.

Ghosts never touch orbs, pads, triggers or coins (so they can't change the real level). When a ghost touches an orb or pad that **you** used, it gets the same boost you got. Results that rely on this, or where the simulation didn't exactly reproduce your run, are marked with `~`.

### Limitations
- Only player 1 is tracked; in dual mode the top player is measured.
- Platformer levels aren't measured.
- Dash orbs, spider orbs/pads and teleport orbs are approximate (`~`).
- Releases are only measured where they matter (ship, wave, robot). Cube releases show `-`.
- If it lags on your device, lower **Max Simulated Players** or set **Precision** to 1 frame.

## Files

```
mod.json                     mod metadata + settings
CMakeLists.txt               build script
src/main.cpp                 game hooks (ticks, inputs, death, reset, pause menu button)
src/Tracker.hpp/.cpp         snapshots, ghost players, frame window calculation
src/Hud.hpp/.cpp             the in-game overlay
src/ReportPopup.hpp/.cpp     run report popup + drag-to-move overlay screen
about.md / changelog.md      shown on the Geode mod page
logo.png                     mod icon
.github/workflows/           GitHub Actions build for all platforms
```

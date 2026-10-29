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

It works like testing a macro: take one input, move it earlier or later, keep **every other input exactly where it was**, and see if you still make it through.

1. Every physics step the mod saves a full copy of player 1's physics state.
2. When you click or release, hidden test players are spawned from those copies. Each one does that input shifted by a fraction of a 240 FPS frame (quarter-frame steps near your timing, coarser for big windows). It then runs on **Geometry Dash's own physics and collision code**, step by step alongside you, and gets all of your later inputs at their real times.
3. A shifted timing **fails** if its test player dies; the report remembers what killed it (spike, block, saw...). It **works** if the test player ends up in exactly the same state as you, or is still alive after the Check Ahead time (1 s by default).
4. The window is the range of timings around yours that work, in **240 FPS frames of real time**. That's independent of your FPS, physics bypass / TPS or time warp, so everyone gets the same numbers.
   - `0-1`: less than one 240 FPS frame. It needs more than 240 FPS (or luck) to hit.
   - `1-2`: at 240 FPS you get 1 or 2 frames to hit it, depending on alignment.
5. An input can't be moved past your previous or next input (a macro can't reorder inputs). If that's what limits a window, the report says "click spacing". A cube's release moves together with its click.

Every input that reaches the player counts: mouse, any key, controller or touch. With **Click Between Frames** the exact time of your click inside the step is used.

### Compatibility
- **Click Between Frames:** supported. Sub-step click times are used, and test players split steps the same way CBF does.
- **MegaHack / Eclipse / other mod menus:** test-player deaths are intercepted before any other mod sees them. Noclip, hitboxes-on-death, death effects and attempt/jump counters are never triggered by them, and noclip doesn't make the windows infinite.
- **Physics bypass / TPS changes:** detected automatically.

### Limitations
- Only player 1 is tracked; in dual mode the top player is measured.
- Platformer levels aren't measured.
- Test players don't use portals. A measurement still running when you enter a portal is finished there ("portal (cut short)").
- Test players don't touch real orbs or pads. If they touch one you used, they get the same boost you got (dash/spider/teleport orbs are marked `~`).
- If it lags, lower **Max Test Players**, raise **Precision**, or shorten **Check Ahead**.

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

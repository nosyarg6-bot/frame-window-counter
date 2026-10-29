# Frame Window Counter

Shows how hard every click and release you make is, measured in **240 FPS frames**, like the frame window videos on YouTube, but automatic and live.

## What is a frame window?
Geometry Dash 2.2 runs its physics at 240 ticks per second, so one frame here is 1/240 of a second (about 4.2 ms). A click's frame window is how many of those frames you could have pressed on and still survived. A **1-2** click means you had between one and two frames of leeway; **0-1** is a sub-frame input.

## Features
- Overlay in the top-left corner listing your latest inputs and their windows, colour coded from magenta (0-1) to cyan (6-10).
- Early / late leeway for each input, e.g. `-1.25 / +0.75`.
- Counter resets when you die; the overlay then shows a summary of the run that just ended.
- **Report** button in the pause menu: every input of your current and previous runs, with a breakdown by difficulty.
- Drag the overlay anywhere with **Move Overlay** in the report.

## Notes
The window is estimated from your real trajectory and the hazards around it (not a full re-simulation), so orbs, pads, portals and moving objects are approximate. Inputs that changed nothing (releasing as the cube, mid-air clicks) are hidden by default. Platformer levels are not supported.

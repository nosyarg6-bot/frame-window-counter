# v1.3.0
- Much more accurate: spikes, saws and blocks are checked with the game's own hitbox shapes (boxes, rotated boxes, circles) on top of the game's death check, so test players can't pass through obstacles anymore (fixes 20-30 frame results on 1-3 frame clicks).
- A hit only counts if your real run didn't touch the same object, so simulation hiccups no longer produce "?" - every click gets an estimate.
- Watch Replay: pause > FW > pick a run > Watch Replay. The level restarts and plays your run back, showing each click and release's frame window as it happens.
- Overlay on/off toggle in the pause menu report.

# v1.2.0
- Fixed huge windows (e.g. 20 frames on a frame perfect): test players now keep all your later inputs and must actually get past the obstacle, instead of stopping at your next click.
- Shifted inputs can't jump past your neighbouring inputs (that used to count as surviving).
- Full player state is copied to test players, so they match your run much more closely.
- Locked to 240 FPS frames of real time on any FPS, TPS / physics bypass and time warp; 0-1 means it needs more than 240 FPS.
- Click Between Frames: the exact sub-frame time of each click is used.
- MegaHack / other mods: test players' deaths no longer trigger noclip, hitboxes on death or death effects.
- Report shows which obstacle limited each click.
- New settings: Check Ahead, Show Test Players (debug, off by default).

# v1.1.0
- Windows are now measured with the game's real physics (hidden test players), not an estimate.
- A window is only finalized once you have passed the obstacle it was for.
- Every input type is detected: mouse, any key, controller, touch.
- Orbs and pads are supported; new Precision and Max Simulated Players settings.

# v1.0.0
- Initial release: automatic frame window counter for clicks and releases, draggable overlay, run report with history.

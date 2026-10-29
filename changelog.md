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

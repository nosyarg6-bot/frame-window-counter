# v1.6.0
- Big accuracy fix: the mod compared positions using a field the game only updates once in a while, so every test player looked like it was drifting 50-250 units away from your real run (seen in your logs). It now uses the real position. This is what broke ship windows, made results show up late, and gave open-space clicks numbers.
- Test players never report a "death" to the game anymore (only the mod's own spike/saw/block check), so MegaHack's hitboxes on death no longer pop up after every click.
- The report opens by itself when you beat a level (separate from the level complete screen, so it works with mods that hide it). Setting: Open Report On Level Complete.
- Frame windows in the level only pop up once known, and only up to a max you choose (Overlay Settings, under "Show frame windows in the level").

# v1.5.0
- Test players now copy the game's "last position" and do the end-of-step rotation update like the real game. Without these they drifted away from your real run on every click (the log showed it), which broke ship windows and made spike jumps read wrong.
- Open space: if nothing but your neighbouring clicks limits an input, it's "free".
- Look-ahead now ends shortly after your next 2 inputs instead of drifting on through open space.
- The "Show test player hitboxes" toggle in Overlay Settings now controls the boxes you see after each click (off by default); test players are always hidden from other hitbox viewers.
- Replay music stays in sync when you skip around, and pauses at the end.
- Report button on the level complete screen.
- More detail in the log for each click (how far the simulation drifted), to keep tuning accuracy.

# v1.4.0
- Accuracy: hits are now compared by how deep you go into an obstacle. Before, a spike your run barely grazed was ignored completely, which turned the hardest (1 frame) clicks into ~20 frame results.
- Look-ahead: every shifted input must also get you through the obstacles of your next 3 inputs (with your later inputs kept as they were), not just a fixed time.
- Replays: left / right arrow keys jump 5 seconds back / forward, a scroll bar at the bottom seeks anywhere, Exit button, and the replay stays on its last frame when it ends.
- Overlay Settings (pause > FW): overlay on/off; when on, position, size and background opacity sliders (plus drag to place).
- New: show every click's frame window in the level while you play (off by default).
- New: hide test player hitboxes near obstacles (on by default) - test players now live outside the level so hitbox viewers don't draw them.

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

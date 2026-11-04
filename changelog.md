# v1.8.5
- Fixed Death Tracker not saving new bests: it hooks the same death function with the same "run first" priority, so it saw the hidden test players' deaths and flagged the attempt as noclip. The mod's hooks now run before every other mod's, and its ignore-damage flag is restored too.

# v1.8.4
- Fixed new bests not saving: the hidden test players collide with damage turned off, which tripped GD's noclip / anti-cheat flags. Those flags are now restored after every test step.
- Replays and clip saving can no longer count as beating the level.

# v1.8.3
- Save keybinds now pick the attempts that actually ended (death or level complete). Before, an attempt you had just started (or one cut off by a restart) counted as your "last attempt", so clips showed only a fraction of a second of it.

# v1.8.2
- Clips and replays from a start position now use the right part of the song (the song's real position is read when the attempt starts).
- Microphone: live level meter in Choose Microphone, recording turns on/off right away when the setting changes, and a clip tells you if the mic was silent.

# v1.8.1
- Fixed clips having no sound in most players (the audio track was missing its AAC decoder config).

# v1.8.0
- Gamemode switching: test players now go through the same portals you did (gamemode, gravity, size and speed portals), so clicks near a portal are measured through it instead of being cut short.
- Ship / wave / swing: a shifted input only has to keep you alive until your next input (plus ~6 frames to react), so windows in tight corridors measure the gap you actually had. Before, every later input was locked in place, which made ship windows far too small.
- Clips: fixed the video only showing a zoomed-in corner of the screen.
- New settings: Clips Folder Location, Also Save Audio As WAV.
- The log now records what killed each failed timing, and where, to keep tuning accuracy.

# v1.7.0
- Accuracy: whole-frame shifts now decide each window, and fractional shifts only refine the last frame. They're ignored if they disagree. This fixes UFO clicks that always read 0.50 and ship clicks/releases that were always cut short on one side.
- Windows at least as big as the max tested now show as "free" instead of 20-30.
- The report no longer pops up when you beat a level; there's a "Frame Windows" button on the level complete screen instead.
- Save clips as MP4 (needs the FFmpeg API mod): "Save MP4" in the report, or keybinds for "last attempt" and "last attempt + end of the one before" (default F7 / F8, changeable in settings).
- Microphone recording: pick a mic, and replays/clips include your click sounds. Separate volumes for game sound and input (mic) sound in clips.
- Setting to turn off the frame window pop-ups in replays and clips.

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

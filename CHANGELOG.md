# Changelog

## [Unreleased]

### Added

- The tracking mode (`Page Up` / `Ctrl+Shift+G`) and the yaw mode (`Page Down` / `Ctrl+Shift+H`) you pick are saved to `HeadTracking.ini` and come back at the next start. `End` / `Ctrl+Shift+Y` still changes the current session only; `EnableOnStartup` decides whether tracking starts on.
- Every hotkey can be rebound or removed in `HeadTracking.ini`, the Ctrl+Shift chords included. The diagnostic `Ctrl+Shift+J` is `InjectModeKey` under `[Dev]`.

### Changed

- `HeadTracking.ini` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `HeadTracking.ini.pre-canonical`. `HeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `HeadTracking.ini.pre-canonical` back over `HeadTracking.ini`, which restores the old file.
- Settings that moved, which the conversion carries over: `LocalSmoothing` and `RemoteSmoothing` to `[Smoothing]`; `[Position] Enabled` to `PositionEnabled` beside a new `RotationEnabled`, the pair the tracking mode is saved as; `LimitX`, `LimitY`, `LimitZ` and `LimitZBack` to `PositionLimitX` and so on, with `LimitY` written to both `PositionLimitY` and `PositionLimitYDown`, since it set the lean both up and down; `[Collision] Radius`, `Channel` and `ReleaseSmoothing` to `CollisionMargin`, `CollisionChannel` and `CollisionReleaseSmoothing` under `[Position]`; `[Aim] TraceChannel` to `AimTraceChannel`; `[Diag] InjectMode` to `[Dev] InjectMode`.
- The lean collision sweep's switch, `[Collision] Enabled`, is now `CollisionEnabled` under `[Position]`. The sweep is still unverified in this game, so a file that turned it on is converted with it off; set `CollisionEnabled=true` to turn it back on.
- `uninstall.cmd` leaves `HeadTracking.ini` in place, so your settings survive a reinstall. It used to delete it.
- Head tracking stays on while you aim down sights, and the lean eases out while the sights are up. The ADS mode cycle is gone: `[Aim] AdsMode` is no longer read, and `Insert` and `Ctrl+Shift+U` do nothing (c7a9c9b).

### Removed

- The reticle settings, `[Reticle] Enabled` and `[Reticle] Targets`. The game's crosshair always moves to where the shot lands, and the mod names the crosshair widgets itself.
- The sensitivity and axis inversion settings: `YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and `InvertRoll` under `[Rotation]`, and `SensitivityX`, `SensitivityY` and `SensitivityZ` under `[Position]`. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

## [0.1.0] - 2026-09-19

### Other

- Hello world

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.0] - 2026-09-06

### Added

- Added head tracking of the camera. The view follows your head in all six
  degrees of freedom while the mouse or controller keeps aiming: only the call
  that builds the frame is handed the head pose, and everything else the game
  asks about your view point still gets the clean mouse-driven one.
- Added crosshair compensation. The game's own crosshair moves to where the
  shot lands, using the live distance to whatever you are pointing at rather
  than a fixed one, so it stays on the spot at any range and whichever way you
  lean. The brackets either side of it and the Tactical Time Dilation ring move
  with it.
- Added field of view compensation, so head tracking no longer feels stronger
  when the game narrows its field of view for a scope or a Tactical Time
  Dilation pull-in. The pose is scaled by the field of view the frame is drawn
  with.
- Added a gameplay gate. Tracking stands down whenever the game raises the
  mouse cursor - the pause screen and the menus - and picks back up on the way
  out.
- Added head tracking in conversations. You can look around while you talk,
  and a head turn moves the picture as far as it does in gameplay, including
  when the camera closes in on the speaker's face.
- Added an optional lean collision sweep. A lean can be swept against the level
  before it is applied, so leaning into a wall does not put the view through
  it. Off by default under `[Collision]` while the trace channel is unconfirmed
  for this game.
- Added window centring for windowed play, so the game window starts centred on
  the work area of the monitor it opened on. The mod waits for the game to
  finish placing the window, leaves fullscreen and borderless windows where they
  are, and leaves alone any window already centred on that work area. Set
  `CenterWindow=0` in `HeadTracking.ini` to turn it off.
- Added two smoothing settings under `[Rotation]`: `LocalSmoothing` (default
  `0.0`) for a tracker running on this machine, and `RemoteSmoothing` (default
  `0.15`) for one on the network. Which applies is chosen per connection from
  the packet source address, so swapping a local OpenTrack instance for a phone
  on WiFi needs no restart.
- Added a rolling log. `HeadTracking.log` covers the session you just played,
  and the one before it is kept beside it as `HeadTracking.prev.log`, so
  relaunching after a crash does not truncate away the record of it.
- Added absolute pose handling with no recenter key. The tracker owns the
  centre and the mod applies the pose it receives as it arrives, so centre your
  view in your tracker app.
- Initial release.

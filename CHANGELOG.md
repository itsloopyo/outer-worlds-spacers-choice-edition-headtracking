# Changelog

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

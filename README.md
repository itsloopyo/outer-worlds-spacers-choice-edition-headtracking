# The Outer Worlds: Spacer's Choice Edition Head Tracking

![The Outer Worlds: Spacer's Choice Edition running with this mod](https://raw.githubusercontent.com/itsloopyo/outer-worlds-spacers-choice-edition-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for The Outer Worlds: Spacer's Choice Edition that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - your head moves the view while the mouse or controller keeps aiming
- **6DOF positional tracking** - lean and peek in every direction as well as looking around
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [The Outer Worlds: Spacer's Choice Edition](https://store.steampowered.com/app/1920490/)
  on Steam.
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack/releases)
  with a webcam or a supported device, or a phone app that sends the same UDP
  pose data.
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **The Outer Worlds: Spacer's Choice Edition**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the latest
   `OuterWorldsSpacersChoiceHeadTracking-v<version>-installer.zip` from the
   [Releases](../../releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds the Steam install and copies the mod and
   its loader into the game folder.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242` (see below).
5. Launch the game.

If the installer cannot find your game, point it at the folder yourself. Either
pass the path as an argument:

```powershell
install.cmd "D:\Games\The Outer Worlds Spacer's Choice"
```

or set the environment variable it checks:

```powershell
$env:OUTER_WORLDS_SPACERS_CHOICE_EDITION_PATH = "D:\Games\The Outer Worlds Spacer's Choice"
```

Either way the path is the game's root folder, the one containing `Indiana\`.

### Manual Installation

To place the files by hand instead, from the installer ZIP:

1. Copy `plugins\OuterWorldsSpacersChoiceHeadTracking.asi` into
   `<game-root>\Indiana\Binaries\Win64\`, alongside
   `Indiana-Win64-Shipping.exe`.
2. Copy `vendor\ultimate-asi-loader\dinput8.dll` into the same folder and
   rename it to `xinput1_3.dll`. The game EXE statically imports
   `XINPUT1_3.dll`, so the loader proxies that import and is loaded at startup.

`HeadTracking.ini` is not in the ZIP. The mod writes it next to the `.asi` on
first launch, with every setting at its default and commented.

Whether a mod manager will deploy this mod depends on that manager's profile
for this game: a manager that deploys into one fixed subfolder cannot reach
`Indiana\Binaries\Win64\` at all. The manual copy above is the route this mod
is tested against.

The Nexus ZIP (`-nexus.zip`) holds the same `.asi` already under
`Indiana\Binaries\Win64\`, so its contents drop straight on top of the game
folder. It does not carry the loader, so install that by hand as in step 2.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimeters, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR and confirm the headset is tracking.
3. In OpenTrack, set **Input** to the SteamVR tracker.
4. Leave **Output** on **UDP over network**, host `127.0.0.1`, port `4242`.

### Webcam Setup

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam, with no
markers and no IR hardware. Select it under **Input**, pick your camera in its
settings, and use the output settings above. How well it tracks depends on your
camera and your lighting, so try it before buying anything.

### Phone App Setup

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first.

What decides whether sending direct is a good idea is how much filtering the app
does before the packet leaves the phone. The mod's smoothing is sized to take
the edge off a clean signal rather than to rescue a noisy one. The test is
quick: send direct, hold your head still, and if the view drifts or shakes,
route the app through
OpenTrack instead. Point it at OpenTrack's **UDP over network** *input* on some
other port, say `5252`, and let OpenTrack's filters and curves clean the feed up
before its output forwards to `127.0.0.1:4242`.

[Headcam](https://headcam.app) is one app that qualifies: I made it so decent
tracking was free for anybody with a phone already in their pocket, and it
filters on-device, so it can send direct. Any app that filters enough noise
works the same way.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Centering

Centering belongs to your tracker. Press the center control there - OpenTrack's
**Center** bind, SteamVR's own reset, or the CENTER button in Headcam - and the
view sits centered.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode (world/local) | `Page Down` | `Ctrl+Shift+H` |

There is no recenter key. The mod keeps no center of its own and applies the
pose it receives exactly as it arrives; centering is done in your tracker, as
described under Centering above.

`Ctrl+Shift+J` cycles a diagnostic that changes which of the game's camera calls
the head pose is injected into. It exists to re-identify the render path after a
game patch and there is no reason to press it in normal play. The cycle has 18
positions, so pressing it once does not return by pressing it again, and one of
the eighteen hands every one of the game's camera calls the head pose, which
turns the aim decoupling off. Restarting the game returns to the shipped
setting.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. Head
movement is scaled to the zoom, so a scope does not magnify it.

Leaning eases out while the sights are up, because it would move your eye off
them.

## Configuration

`HeadTracking.ini` is written next to the mod on first launch, at
`<game-root>\Indiana\Binaries\Win64\HeadTracking.ini`. Edit values and restart
the game to apply them.

```ini
; Outer Worlds: Spacer's Choice Edition Head Tracking - configuration
; Edit values, restart the game to apply.

[Network]
UdpPort=4242

[General]
EnableOnStartup=1
; Yaw mode the mod starts in. 1 = horizon-locked: head yaw turns the
; view about the world up-axis, so the horizon stays level however far
; the mouse has pitched the camera. 0 = camera-local, which leans the
; horizon on a pitched turn. Page Down switches it for the session.
WorldSpaceYaw=1
; Centre the game window on your monitor's work area at startup. Only
; ever moves a windowed game - fullscreen and borderless are left alone,
; and so is a window already centred there. Set to 0 to keep the window
; wherever the game or you put it.
CenterWindow=1

[Hotkeys]
; Virtual-key code for the yaw-mode toggle. 0x22 is Page Down. It
; cannot be a key this mod already uses - End (0x23) or Page Up (0x21)
; - because one press would then fire both actions.
; Set it to one of those and Page Down is kept, with a line saying so
; in HeadTracking.log.
YawModeKey=0x22

[Rotation]
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=0
InvertPitch=0
InvertRoll=0
; Smoothing is picked per connection from the packet source address and
; covers rotation and position alike. 0.0 = none, 1.0 = heavy.
; LocalSmoothing:  tracker runs on this machine (loopback).
; RemoteSmoothing: tracker is a remote device on the network.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
Enabled=1
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Lean limits in meters. Z is asymmetric: more room to lean forward
; than back, so the view does not end up inside your own character.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
LimitZBack=0.10

[Reticle]
; The game draws its crosshair at the middle of the picture, which stops
; being where the round goes once the head moves the view. These are the
; UMG widgets moved to meet the shot. Names come from a running game -
; set [Dev] WidgetDump=1 and read HeadTracking.log. Name or Name@Outer,
; comma separated, where Outer is text found in the widget's chain of
; parent objects joined with / (for example Reticle/WidgetTree).
Enabled=1
Targets=Crosshair@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,ReticuleInteract@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,CauseDamageWidget@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,StealthOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,TTDOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,TTDDTOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance

[Aim]
; The cast that gives the crosshair the live distance to what you are
; pointing at. Without it the mark is right at one range only.
; MaxDistance is in centimeters; past it the crosshair marks the aim
; direction instead of a point.
TraceChannel=0
MaxDistance=20000

[Collision]
; Stops a lean putting the view inside a wall. Off until the sweep has
; been confirmed engaging on real geometry in this game - the log says
; so on every contact. Radius is how far off a surface the eye is held,
; in centimeters. ReleaseSmoothing is how quickly the lean reopens once
; an obstruction clears; tightening is always instant.
Enabled=0
Radius=12.0
Channel=0
ReleaseSmoothing=0.9

[Dev]
WidgetDump=0
```

Field of view is the game's own setting, under Settings > Graphics > Display.
Set it wherever you like: your head moves the view by the same amount at any
field of view, and the slider takes effect as you drag it, with no restart.

## Troubleshooting

**Mod not loading**

- Confirm `xinput1_3.dll` and `OuterWorldsSpacersChoiceHeadTracking.asi` are
  both in `<game-root>\Indiana\Binaries\Win64\`.
- Check `HeadTracking.log`, written next to the game EXE. A line reading
  "fingerprint matches ... but its offsets are not yet derived" means the mod
  recognized your game build but the camera hook for it has not shipped yet;
  "game build is NEWER/OLDER than any profile" means a patch moved the offsets.
  File an issue with the log attached.
- In any of those cases the mod installs no hooks at all and the game runs
  exactly as it does unmodded. Only Steam builds are fingerprinted, and of those
  only the 2026-08-04 one carries camera offsets, so an Epic, GOG or Xbox copy -
  or the 2026-05-05 Steam build - loads the mod and leaves it dormant.
- The log starts fresh every launch, so it only covers the session you just
  played. If the game crashed, attach `HeadTracking.prev.log` from the same
  folder too - that is the crashed session.

**No tracking response**

- Confirm your tracker is started and sending to `127.0.0.1:4242`.
- Check tracking is not toggled off: press `End` or `Ctrl+Shift+Y`.
- If another program is still holding port `4242` when the game starts (usually
  a previous game you have not closed), nothing needs restarting.
  `HeadTracking.log` records the bind failure with the reason the OS gave, and
  the mod retries every 500ms, so closing the other program brings tracking up
  within about half a second and logs a `tracking is live` line when it does.

**Jittery or unstable tracking**

- A phone sending direct may jitter, depending on how much the app filters
  before the packet leaves it. Route it through OpenTrack so its filters and
  curves can clean the feed up, as described under Phone App Setup.
- Raise `RemoteSmoothing` for a tracker on the network, or `LocalSmoothing` for
  one on this machine. Both live in the `[Rotation]` section.

**Leaning hard into a wall puts the view through it**

- The lean collision sweep is what stops that, and it ships turned off. Set
  `Enabled=1` in `[Collision]`. `Radius` is how far off a surface the eye is
  held, in centimeters; `Channel` is which collision channel the sweep tests
  against; `ReleaseSmoothing` is how quickly the lean reopens once an
  obstruction clears.
- `HeadTracking.log` says what the sweep is doing: `lean-clamp: contact=yes`
  when it is holding the eye off geometry, `contact=no` when the room is open,
  and `query=FAILED (lean unclamped)` when the sweep could not run at all. If
  channel `0` stays silent against a solid wall, try `1` and `2`.

**View sits off center, or yaw feels wrong**

- The view sits off center: center it in your tracker app, not in the game.
- Yaw feels wrong when you are looking a long way up or down: press `Page Down`
  (or `Ctrl+Shift+H`) to switch yaw modes. World-locked, the default, turns your
  head about the world up-axis so the horizon stays level; camera-local turns it
  about the camera's own up-axis, which leans the horizon instead.

**The weapon is off to one side when I aim down sights**

- Your head is turned: the weapon stays on your aim and you are looking past
  it. Turn back to it, or move your aim to where you are looking.

**The game window moved when you launched**

- By design, and only when you play windowed. Once the game has finished
  placing its window, the mod centers it on the work area of the monitor it
  opened on - the screen minus the taskbar. A
  fullscreen or borderless window is left alone, and so is one already centered
  on that work area. A window the game centered on the whole monitor sits half a
  taskbar low, so it does get nudged up. Set `CenterWindow=0` in
  `HeadTracking.ini` to leave your window where it is.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs, and also `HeadTracking.ini` and
the two log files, so any settings you tuned are gone with it. The ASI loader is
only removed if the installer put it there. Use `uninstall.cmd /force` to remove
it anyway and leave the game folder fully vanilla.

## Building from Source

Prerequisites: [pixi](https://pixi.sh), Visual Studio 2022 build tools, and Git.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/outer-worlds-spacers-choice-edition-headtracking
cd outer-worlds-spacers-choice-edition-headtracking
pixi run build
pixi run package
```

Outputs land in `release/`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Game by **Obsidian Entertainment**, published by **Private Division**. Buy it
  on [Steam](https://store.steampowered.com/app/1920490/).
- Loader: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- Protocol: [OpenTrack](https://github.com/opentrack/opentrack).
- Shared infrastructure from
  [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core).

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Obsidian
Entertainment or Private Division. Use at your own risk.

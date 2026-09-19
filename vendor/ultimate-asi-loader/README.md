# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader, the install-time source of truth.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.4`
- Commit: `6b440669144c4a0bef5718ab155df160d231cd42`
- Asset: `Ultimate-ASI-Loader_x64.zip`
- dinput8.dll SHA-256: `fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7`
- Fetched at: 2026-09-08T08:17:45.7909522+01:00

`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it to
`<game>/Indiana/Binaries/Win64/xinput1_3.dll`, the ASI_LOADER_NAME pinned in its CONFIG
BLOCK. XINPUT1_3 is statically imported by the game exe, so the loader is brought in from
the game directory early, and being controller-only it does not collide with a dxgi or
d3d11 interposer.

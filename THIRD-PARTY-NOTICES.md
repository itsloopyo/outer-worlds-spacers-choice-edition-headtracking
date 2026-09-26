# Third-Party Notices

OuterWorldsSpacersChoiceHeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

This repository contains no game code, no extracted game assets and no game
data files. The README embeds a short gameplay clip by URL; see the footage
section below.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| Ultimate ASI Loader | v9.7.4 | MIT | Bundled verbatim in the installer ZIP |
| injector | `3a384e8` (inside Ultimate ASI Loader v9.7.4) | Zlib | Compiled into the vendored dinput8.dll |
| miniz | 11.0.2 (inside Ultimate ASI Loader v9.7.4) | MIT | Compiled into the vendored dinput8.dll |
| MinHook | v1.3.3 | BSD-2-Clause | Compiled into `OuterWorldsSpacersChoiceHeadTracking.asi` |
| cameraunlock-core | b4df73a5d8076968fcbf7e4088dd49db11a2684e | MIT | Compiled into `OuterWorldsSpacersChoiceHeadTracking.asi`, and its installer scripts ship verbatim in the installer ZIP |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |
| Unreal Engine | 4.27 | Unreal Engine EULA | Not bundled; type and member names only |

---

## Ultimate ASI Loader

Vendored at `vendor/ultimate-asi-loader/`, shipped in the installer ZIP and used as the
install-time source. Taken from the upstream release asset untouched; the
upstream licence file ships beside it at `vendor/ultimate-asi-loader/LICENSE`.

- **Version:** `v9.7.4` (commit `6b440669144c4a0bef5718ab155df160d231cd42`,
  SHA-256 `fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7`)
- **License:** `MIT`
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Renamed to `xinput1_3.dll` beside the game exe so it loads our
  `.asi` at startup.
- **Bundled:** yes. Bundled in the release ZIP and used as the install-time
  source; `install.cmd` extracts the vendored copy and never fetches a loader
  from the network.

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

That `dinput8.dll` is a static binary and is not one component. The
`Ultimate-ASI-Loader-x64` target in `premake5.lua` at v9.7.4 compiles
`external/injector/minhook/src/**.c`,
`external/injector/utility/FunctionHookMinHook.cpp` and `external/miniz/miniz.c`
alongside the loader's own sources, so redistributing it redistributes MinHook,
injector and miniz as well, and each has its own section in this file.
MemoryModule, d3d8to9 and the minidx9 DirectX headers belong to the 32-bit
target only and are absent from this binary. The MinHook section covers the copy
inside the loader as well as any linked into the mod itself; the licence text is
the same.

---

## injector

Compiled into the vendored `dinput8.dll`. The loader's `FunctionHookMinHook`
wrapper, which the `Ultimate-ASI-Loader-x64` target compiles from
`external/injector/utility/FunctionHookMinHook.cpp`, and the MinHook submodule
that repository carries. Nothing in this repository calls or links it; it ships
only inside that binary.

- **Version:** commit `3a384e8d1b575c09383b0fab8bd92e34cb654949`, the
  submodule Ultimate ASI Loader v9.7.4 pins at `external/injector/`
- **License:** `Zlib`
- **Upstream:** https://github.com/ThirteenAG/injector
- **Usage:** Compiled into the vendored `dinput8.dll` by the loader's own
  build. Nothing in this repository calls or links it.
- **Bundled:** yes. Ships inside the vendored loader binary in the release ZIP.

The binary is unaltered upstream, so the "altered source versions" condition
below does not arise. It is reproduced whole regardless.

```
Copyright (C) 2012-2014 LINK/2012 <dma_2012@hotmail.com>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
```

---

## miniz

Compiled into the vendored `dinput8.dll`. Zip reading for the loader's
`LoadVirtualFilesFromZip` path, which the `Ultimate-ASI-Loader-x64` target
compiles from `external/miniz/miniz.c`. Nothing in this repository calls or
links it; it ships only inside that binary.

- **Version:** 11.0.2, as vendored at `external/miniz/` in Ultimate ASI Loader
  v9.7.4
- **License:** `MIT`
- **Upstream:** https://github.com/richgel999/miniz
- **Usage:** Zip reading for the loader's `LoadVirtualFilesFromZip` path.
  Nothing in this repository calls or links it.
- **Bundled:** yes. Ships inside the vendored loader binary in the release ZIP.

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## MinHook

Fetched from upstream at configure time and compiled into `OuterWorldsSpacersChoiceHeadTracking.asi`.

- **Version:** `v1.3.3` (commit `9fbd087432700d73fc571118d6a9697a36443d88`)
- **License:** `BSD-2-Clause`
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Trampoline hook on the game's view-point call, fetched by CMake at
  configure time and statically linked into the mod.
- **Bundled:** yes. Compiled into the `OuterWorldsSpacersChoiceHeadTracking.asi`
  that ships in the release ZIP; no separate MinHook file is installed.

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that MinHook's own
`src/hde/` is built from. Both notices appear below exactly as upstream ships them.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `OuterWorldsSpacersChoiceHeadTracking.asi`. Our own code,
MIT licensed, reproduced here so the notices are complete.

- **Version:** commit `b4df73a5d8076968fcbf7e4088dd49db11a2684e`
- **License:** `MIT`
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared tracking pipeline (UDP receiver, pose interpolation,
  smoothing, camera and lean helpers), compiled into the mod. Its installer
  scripts also run the install, from the ZIP rather than from the binary.
- **Bundled:** yes, in two forms. Its C++ half is compiled into the
  `OuterWorldsSpacersChoiceHeadTracking.asi` that ships in the release ZIP. Its
  install and uninstall bodies, `find-game.ps1`, `GamePathDetection.psm1` and
  `games.json` ship as source under `shared/` in the installer ZIP, staged there
  by the packager; `install.cmd` and `uninstall.cmd` at the ZIP root are thin
  wrappers that call them.

```
MIT License

Copyright (c) 2026 itsloopyo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

- **Version:** n/a. The UDP pose datagram layout only, which is not versioned
  here.
- **License:** `ISC`
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** We implement its UDP pose datagram layout so that OpenTrack and
  compatible trackers can drive the mod.
- **Bundled:** no. No OpenTrack code, headers or binaries are copied, linked or
  redistributed.

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Unreal Engine

Not bundled, not linked, and no part of it is redistributed here. The Outer
Worlds: Spacer's Choice Edition is built on Unreal Engine 4.27, and Epic Games
publishes that engine's source. That source release is where the engine type
and member names this repository uses come from - `FMinimalViewInfo` and its
`Location` / `Rotation` / `FOV` / `AspectRatio` fields,
`APlayerController::GetPlayerViewPoint`, `UObject::ProcessEvent`,
`UStruct::PropertiesSize`, `FField::ClassPrivate` and the rest - along with the
field ordering and the `FRotator` pitch/yaw/roll convention the camera boundary
converts to.

- **Version:** 4.27, the engine version the game ships on
- **License:** Unreal Engine EULA
- **Upstream:** https://www.unrealengine.com
- **Usage:** Names, declaration order and rotation conventions only. No engine
  source, headers or binaries are copied, linked or redistributed.
- **Bundled:** no.

Naming it is what makes the boundary auditable. The names and the layout
conventions are Epic's, and are stated as such; the numeric addresses and
offsets in `src/builds/` are not - those are measurements taken against a
legitimately owned copy of the shipped game, recorded as numbers.

---

## The Outer Worlds: Spacer's Choice Edition footage

- **File:** `assets/readme-clip.gif`
- **Rights holder:** Obsidian Entertainment and Private Division.
- **Purpose:** a short gameplay clip embedded at the top of the README so a
  visitor can see what the mod does.
- **Distribution:** embedded in the README by URL and hosted alongside this
  repository. It ships in neither release ZIP - the installer packager copies
  README.md, LICENSE, CHANGELOG.md and THIRD-PARTY-NOTICES.md, and the Nexus
  packager copies README.md, LICENSE and THIRD-PARTY-NOTICES.md; neither copies
  `assets/`.
- **Licence:** none is granted or implied over it. It will be removed on
  request by any rights holder.

## The Outer Worlds: Spacer's Choice Edition

The Outer Worlds: Spacer's Choice Edition and all related names, logos,
characters and marks are trademarks of their respective owners. They are used
here only to identify the game this mod applies to, which is nominative use
and not a claim of any right in them. This project is an unofficial, fan-made
modification. It is not affiliated with, endorsed by, or sponsored by the
game's developers, its publishers, its engine vendor, or any other rights
holder. It redistributes no game code, no extracted game assets and no
proprietary DLLs, and it requires a legitimately purchased copy of the game.
The structure
offsets and function addresses in `src/builds/` were measured by the authors
against a legitimately owned copy, and are recorded as numbers; the engine type
and member names they are labelled with come from Epic's published Unreal
Engine source, as set out above. No game source, no decompiler output and no
disassembly listing is stored in this repository.

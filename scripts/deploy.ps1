# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Deploys the built .asi and the vendored ASI loader into the local Outer
    Worlds: Spacer's Choice Edition install (the dev loop).

.DESCRIPTION
    Unattended: no prompts, exit 1 with a one-line diagnostic on any failure.

    Resolution order matches install.cmd's: an explicitly supplied path wins,
    then OUTER_WORLDS_SPACERS_CHOICE_EDITION_PATH, then Find-GamePath's walk
    (env var -> Steam appmanifest -> Steam library folders -> other stores)
    driven by the games.json entry.

.PARAMETER GamePath
    Install root of the game. Omit to detect it.
#>

[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')

Import-Module -Name (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

$modName = 'OuterWorldsSpacersChoiceHeadTracking'
$gameId = 'outer-worlds-spacers-choice-edition'

# Both read from the files that own them rather than restated here, so a version
# bump cannot leave the dev deploy writing a stale state file.
$modVersion = ([regex]::Match(
    (Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw),
    'project\(OuterWorldsSpacersChoiceHeadTracking VERSION ([0-9]+\.[0-9]+\.[0-9]+)')).Groups[1].Value
if (-not $modVersion) {
    Write-Host 'ERROR: could not read the project version out of CMakeLists.txt.' -ForegroundColor Red
    exit 1
}
$loaderVersion = ([regex]::Match(
    (Get-Content -LiteralPath (Join-Path $root 'scripts/install.cmd') -Raw),
    'set "ASI_LOADER_VERSION=([^"]+)"')).Groups[1].Value
if (-not $loaderVersion) {
    Write-Host 'ERROR: could not read ASI_LOADER_VERSION out of scripts/install.cmd.' -ForegroundColor Red
    exit 1
}

# Whether THIS run is what put the proxy DLL there.
$loaderWasOurs = $false

$asi = Join-Path $root "build/Release/$modName.asi"
if (-not (Test-Path -LiteralPath $asi)) {
    Write-Host "ERROR: build output not found at $asi. Run 'pixi run build' first." -ForegroundColor Red
    exit 1
}

$cfg = Get-GameConfig -GameId $gameId
if (-not $cfg) {
    Write-Host "ERROR: cameraunlock-core/data/games.json has no '$gameId' entry. Update the submodule." -ForegroundColor Red
    exit 1
}
$exeRelPath = $cfg.Executable

if ($GamePath) {
    if (-not (Test-Path -LiteralPath $GamePath -PathType Container)) {
        Write-Host "ERROR: supplied game path is not a directory: $GamePath" -ForegroundColor Red
        exit 1
    }
} elseif ($env:OUTER_WORLDS_SPACERS_CHOICE_EDITION_PATH) {
    $GamePath = $env:OUTER_WORLDS_SPACERS_CHOICE_EDITION_PATH
} else {
    $GamePath = Find-GamePath -GameId $gameId
}

if (-not $GamePath) {
    Write-Host 'ERROR: game install not found. Set OUTER_WORLDS_SPACERS_CHOICE_EDITION_PATH or pass the install root as the first argument.' -ForegroundColor Red
    exit 1
}

$exe = Join-Path $GamePath $exeRelPath
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "ERROR: game exe not found at $exe." -ForegroundColor Red
    exit 1
}
$exeDir = Split-Path -LiteralPath $exe

Write-Host "Deploying to $exeDir" -ForegroundColor Cyan

# ASI_LOADER_NAME in install.cmd is xinput1_3.dll: Indiana-Win64-Shipping.exe
# statically imports XINPUT1_3.dll and does not import dinput8.dll, and xinput
# is loaded early with far less contention than the dxgi/d3d11 slots the
# upscaler and overlay interposers take. The vendored artifact ships as
# dinput8.dll and is renamed on deploy, exactly as the installer does it.
$loader = Join-Path $exeDir 'xinput1_3.dll'
if (-not (Test-Path -LiteralPath $loader)) {
    $vendored = Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll'
    if (-not (Test-Path -LiteralPath $vendored)) {
        Write-Host "ERROR: vendored loader not found at $vendored. Run 'pixi run update-deps'." -ForegroundColor Red
        exit 1
    }
    Copy-Item -LiteralPath $vendored -Destination $loader -Force
    $loaderWasOurs = $true
    Write-Host '  Deployed Ultimate ASI Loader -> xinput1_3.dll' -ForegroundColor Green
}

Copy-Item -LiteralPath $asi -Destination (Join-Path $exeDir "$modName.asi") -Force
Write-Host "Deployed $modName.asi to $exeDir" -ForegroundColor Green

# The state file install.cmd would have written, because `pixi run uninstall`
# runs the SAME uninstall body and that body decides whether to remove the proxy
# DLL by reading framework.installed_by_us out of it. Without the file the test
# fails closed, the loader is deliberately left behind, and the printed reason -
# "was not installed by this mod" - is wrong. A dev who ran `pixi run install`
# then `pixi run uninstall` was left with an xinput1_3.dll interposing on their
# game with no .asi for it to load, recoverable only with /force.
#
# installed_by_us stays true once true: it records whether the loader sitting
# there is ours, and a redeploy over our own loader does not change that.
$statePath = Join-Path $GamePath '.headtracking-state.json'
if (-not $loaderWasOurs -and (Test-Path -LiteralPath $statePath)) {
    $loaderWasOurs =
        (Get-Content -LiteralPath $statePath -Raw) -match '"installed_by_us"\s*:\s*true'
}
$stateJson = @{
    schema_version = 1
    framework = @{
        type = 'ASILoader'
        installed_by_us = [bool]$loaderWasOurs
        version = $loaderVersion
    }
    mod = @{
        id = $gameId
        name = $modName
        version = $modVersion
        installed_at = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    }
} | ConvertTo-Json -Depth 4

# WriteAllText with a BOM-less UTF8Encoding, for the reason package-release.ps1
# and release.ps1 both give: PowerShell 5.1's `-Encoding utf8` emits a BOM, and
# a strict JSON reader rejects the leading EF BB BF.
[System.IO.File]::WriteAllText(
    $statePath, $stateJson, (New-Object System.Text.UTF8Encoding($false)))

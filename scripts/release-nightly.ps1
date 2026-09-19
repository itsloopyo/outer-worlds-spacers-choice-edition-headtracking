# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$cmakeFile = Join-Path $ProjectRoot 'CMakeLists.txt'
$versionMatch = Select-String -Path $cmakeFile -Pattern 'project\(OuterWorldsSpacersChoiceHeadTracking VERSION ([0-9]+\.[0-9]+\.[0-9]+)'
if (-not $versionMatch) {
    throw "Could not extract version from $cmakeFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'outer-worlds-spacers-choice-edition' `
    -ModName 'OuterWorldsSpacersChoiceHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty

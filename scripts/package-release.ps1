# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Packages the release ZIPs into release/.

.DESCRIPTION
    Unattended: no prompts, non-zero exit on any failure.

    The vendored Ultimate ASI Loader is consumed exactly as committed under
    vendor/ - refreshing it is `pixi run update-deps`, a deliberate dev action
    with a commit attached, never a packaging side effect.

    Two ZIPs come out of this:
      release/<ModName>-v<version>-installer.zip  install.cmd + payload + docs
      release/<ModName>-v<version>-nexus.zip      the deploy subtree alone
#>

$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force
$buildDir = Join-Path $projectDir 'build/Release'
$releaseDir = Join-Path $projectDir 'release'

$asi = Join-Path $buildDir 'OuterWorldsSpacersChoiceHeadTracking.asi'
if (-not (Test-Path $asi)) {
    throw "Built .asi not found at $asi. Run 'pixi run build' first."
}

# CMakeLists.txt is the canonical version source; release.yml re-reads it
# through this same helper to check the tag against the built artifact.
$version = Get-ProjectVersion -Source 'cmake' -Path (Join-Path $projectDir 'CMakeLists.txt')

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

# Stage installer ZIP contents in a temp folder. Both staging trees are removed
# in the finally below rather than at the end of the happy path: a throw between
# here and there used to leave a few megabytes of staged payload in %TEMP% under
# a fresh GUID on every failed run, with nothing to collect it.
$stage = Join-Path $env:TEMP "owsc-ht-stage-$([Guid]::NewGuid().ToString('N'))"
$nexusStage = Join-Path $env:TEMP "owsc-ht-nexus-$([Guid]::NewGuid().ToString('N'))"
try {
    New-Item -ItemType Directory -Path $stage | Out-Null

    # Plugin payload
    $plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
    Copy-Item -Force $asi (Join-Path $plugins.FullName 'OuterWorldsSpacersChoiceHeadTracking.asi')

    # Vendor (loader)
    $vendorSrc = Join-Path $projectDir 'vendor/ultimate-asi-loader'
    $vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader')
    if (-not (Test-Path $vendorSrc)) {
        throw 'vendor/ultimate-asi-loader is missing. install.cmd extracts the loader from it, so a ZIP built without it installs a mod that never loads. Run pixi run update-deps and commit the result.'
    }
    Copy-Item -Force (Join-Path $vendorSrc '*') $vendorDst.FullName -Recurse

    # Scripts + find-game shim
    Copy-Item -Force (Join-Path $projectDir 'scripts/install.cmd') $stage
    Copy-Item -Force (Join-Path $projectDir 'scripts/uninstall.cmd') $stage
    # install.cmd and uninstall.cmd are thin wrappers: the body they call lives in
    # shared/ at the ZIP root, and without it the installer aborts at its own layout
    # check and exits 1 on every run. Copy-SharedBundle stages every body there,
    # alongside find-game.ps1, GamePathDetection.psm1 and games.json at the paths
    # find-game.ps1 actually looks in.
    Copy-SharedBundle -StagingDir $stage

    # Launcher manifest (lopari ingests this from the ZIP root). Stamp the
    # authoritative version from CMakeLists.txt so the shipped manifest never
    # drifts from the release.
    $manifest = Get-Content -Raw (Join-Path $projectDir 'launcher-manifest.json') | ConvertFrom-Json
    $manifest.mod_info.version = $version
    $manifestJson = $manifest | ConvertTo-Json -Depth 10
    # WriteAllText with a BOM-less UTF8Encoding: PowerShell 5.1's -Encoding utf8
    # emits a BOM, and a strict JSON reader rejects the leading EF BB BF.
    [System.IO.File]::WriteAllText((Join-Path $stage 'launcher-manifest.json'), $manifestJson, (New-Object System.Text.UTF8Encoding($false)))

    # Docs
    foreach ($f in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
        Copy-Item -Force (Join-Path $projectDir $f) $stage
    }

    $installerZip = Join-Path $releaseDir "OuterWorldsSpacersChoiceHeadTracking-v$version-installer.zip"
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
    Write-Host "Built $installerZip" -ForegroundColor Green

    # Nexus ZIP: only the files that drop into Indiana/Binaries/Win64/
    $nexusInner = New-Item -ItemType Directory -Path (Join-Path $nexusStage 'Indiana/Binaries/Win64') -Force
    Copy-Item -Force $asi $nexusInner.FullName
    $nexusZip = Join-Path $releaseDir "OuterWorldsSpacersChoiceHeadTracking-v$version-nexus.zip"
    # The Nexus ZIP is a binary distribution too: the licences of everything
    # compiled into or bundled with the payload require their notices to travel
    # with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
    foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
        $noticeSrc = Join-Path $projectDir $noticeDoc
        if (-not (Test-Path $noticeSrc)) {
            throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
        }
        Copy-Item $noticeSrc -Destination $nexusStage -Force
        Write-Host "  $noticeDoc" -ForegroundColor Green
    }
    Compress-Archive -Path (Join-Path $nexusStage '*') -DestinationPath $nexusZip -Force
    Write-Host "Built $nexusZip" -ForegroundColor Green

} finally {
    foreach ($tmp in @($stage, $nexusStage)) {
        if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

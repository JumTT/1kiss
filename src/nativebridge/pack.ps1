#!/usr/bin/env pwsh
# pack.ps1 — local one-shot: build the full curl chain + the nativebridge module
# for the current host platform, then report the produced native lib for quick
# local verification. (For a full multi-platform Unity package, use the
# `nativebridge` CI workflow, which runs src/nativebridge/dist1.sh.)
#
# Usage:
#   ./src/nativebridge/pack.ps1                       # host platform, x64
#   ./src/nativebridge/pack.ps1 -platform win32 -arch x64
param(
    $platform = $(if ($IsWindows) { 'win32' } elseif ($IsMacOS) { 'osx' } else { 'linux' }),
    $arch = 'x64',
    $version = '1.0.0',
    [switch]$rebuild
)

$ErrorActionPreference = 'Stop'
# src/nativebridge/pack.ps1 -> repo root is two levels up.
$repo_root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$env:NATIVEBRIDGE = '1'
$libs = 'zlib,boringssl,nghttp3,ngtcp2,nghttp2,curl,nativebridge'

Write-Host "nativebridge pack: building $platform/$arch (libs=$libs)"
$build = Join-Path $repo_root 'build.ps1'
& $build -p $platform -a $arch -libs $libs -rebuild:$rebuild
if ($LASTEXITCODE) { throw "build.ps1 failed" }

# Locate the produced native lib.
$install = Join-Path $repo_root "install_${platform}_${arch}/nativebridge"
Write-Host "nativebridge pack: install dir = $install"
Get-ChildItem -Recurse $install -Include *.dll, *.so, *.dylib, *.a -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host "  produced: $($_.FullName)" }

Write-Host "nativebridge pack: for a full multi-platform Unity package, run the 'nativebridge' CI workflow (src/nativebridge/dist1.sh)."

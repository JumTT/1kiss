# patch1.ps1 — stage the Rust nativebridge sources into the build dir.
#
# For a normal (fetched) lib, build.ps1 would clone/extract sources and fetch.ps1
# writes the `_1kiss` sentry. nativebridge is a `local: true` module: build.ps1
# only creates an empty buildsrc/nativebridge dir, so here we copy our in-repo
# rust/ tree into it and write the sentry ourselves (its "ver:" line drives
# build.ps1's up-to-date cache check).
param(
    $lib_src,
    $ver
)

$src_rust = Join-Path $PSScriptRoot 'rust'

Write-Host "nativebridge patch1: staging Rust sources '$src_rust' -> '$lib_src'"

if (!(Test-Path $lib_src)) {
    New-Item $lib_src -ItemType Directory -Force | Out-Null
}

Copy-Item -Path (Join-Path $src_rust 'Cargo.toml') -Destination $lib_src -Force
Copy-Item -Path (Join-Path $src_rust 'build.rs') -Destination $lib_src -Force
Copy-Item -Path (Join-Path $src_rust 'src') -Destination $lib_src -Recurse -Force

$sentry = Join-Path $lib_src '_1kiss'
[System.IO.File]::WriteAllText($sentry, "ver: $ver")

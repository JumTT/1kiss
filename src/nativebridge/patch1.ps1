# patch1.ps1 — stage the local nativebridge sources into the build dir.
#
# For a normal (fetched) lib, build.ps1 would clone/extract sources and fetch.ps1
# writes the `_1kiss` sentry. nativebridge is a `local: true` module: build.ps1
# only creates an empty buildsrc/nativebridge dir, so here we copy our in-repo
# native/ tree into it and write the sentry ourselves (its "ver:" line drives
# build.ps1's up-to-date cache check).
param(
    $lib_src,
    $ver
)

$src_native = Join-Path $PSScriptRoot 'native'

Write-Host "nativebridge patch1: staging '$src_native' -> '$lib_src'"

# Copy the whole native/ tree (CMakeLists.txt + sources) into the build dir.
Copy-Item -Path (Join-Path $src_native '*') -Destination $lib_src -Recurse -Force

# Write the version sentry so build.ps1 can copy it to the install dir and later
# recognize cached, up-to-date artifacts.
$sentry = Join-Path $lib_src '_1kiss'
[System.IO.File]::WriteAllText($sentry, "ver: $ver")

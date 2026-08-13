# install1.ps1 — copy Rust build artifacts to the install tree.
#
# Args: $install_dir $lib_src
# Called by build.ps1 after a successful build. Places the native lib at the
# same location the old CMake build produced:
#   Windows:  install_dir/bin/NativeBridge.dll
#   Linux/Android: install_dir/lib/libNativeBridge.so
#   macOS:    install_dir/lib/libNativeBridge.dylib
#   iOS/tvOS: install_dir/lib/libNativeBridge.a   (static archive)

param(
    $install_dir,
    $lib_src
)

# Re-derive target info from globals build.ps1 sets.
$target_os = $Global:target_os
$target_cpu = $Global:target_cpu

function fail($msg) { throw "nativebridge install: $msg" }

if (-not $target_os) { fail 'target_os global not set' }
if (-not $target_cpu) { fail 'target_cpu global not set' }

# Map to Rust target triple (MUST stay in sync with build1.ps1).
$rust_target = $null
if ($target_os -eq 'win32') {
    if ($target_cpu -eq 'x64' -or $target_cpu -eq 'amd64') { $rust_target = 'x86_64-pc-windows-msvc' }
    elseif ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-pc-windows-msvc' }
}
elseif ($target_os -eq 'linux') {
    if ($target_cpu -eq 'x64') { $rust_target = 'x86_64-unknown-linux-gnu' }
    elseif ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-unknown-linux-gnu' }
}
elseif ($target_os -eq 'android') {
    if ($target_cpu -eq 'armv7') { $rust_target = 'armv7-linux-androideabi' }
    elseif ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-linux-android' }
    elseif ($target_cpu -eq 'x86') { $rust_target = 'i686-linux-android' }
    elseif ($target_cpu -eq 'x64') { $rust_target = 'x86_64-linux-android' }
}
elseif ($target_os -eq 'osx') {
    if ($target_cpu -eq 'x64') { $rust_target = 'x86_64-apple-darwin' }
    elseif ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-apple-darwin' }
}
elseif ($target_os -eq 'ios') {
    if ($Global:is_ios_sim) {
        if ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-apple-ios-sim' }
        elseif ($target_cpu -eq 'x64') { $rust_target = 'x86_64-apple-ios' }
    } else {
        if ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-apple-ios' }
    }
}
elseif ($target_os -eq 'tvos') {
    if ($Global:is_tvos_sim) {
        if ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-apple-tvos-sim' }
        elseif ($target_cpu -eq 'x64') { $rust_target = 'x86_64-apple-tvos' }
    } else {
        if ($target_cpu -eq 'arm64') { $rust_target = 'aarch64-apple-tvos' }
    }
}
if (-not $rust_target) { fail "unsupported target: $target_os/$target_cpu" }

$release_dir = Join-Path $lib_src "target/$rust_target/release"
Write-Host "nativebridge install: release_dir=$release_dir"

# Determine output subdir + filename per platform.
$use_static = ($target_os -eq 'ios' -or $target_os -eq 'tvos')
if ($use_static) {
    $out_subdir = 'lib'
    $src_name = 'libNativeBridge.a'
}
elseif ($target_os -eq 'win32') {
    $out_subdir = 'bin'
    $src_name = 'NativeBridge.dll'
}
elseif ($target_os -eq 'osx') {
    $out_subdir = 'lib'
    $src_name = 'libNativeBridge.dylib'
}
else {
    $out_subdir = 'lib'
    $src_name = 'libNativeBridge.so'
}

$src_path = Join-Path $release_dir $src_name
if (-not (Test-Path $src_path -PathType Leaf)) {
    fail "build artifact not found: $src_path"
}

$dst_dir = Join-Path $install_dir $out_subdir
if (-not (Test-Path $dst_dir -PathType Container)) {
    New-Item $dst_dir -ItemType Directory -Force | Out-Null
}

$dst_name = if ($use_static) { 'libNativeBridge.a' } else { $src_name }
$dst_path = Join-Path $dst_dir $dst_name
Copy-Item -Path $src_path -Destination $dst_path -Force
Write-Host "nativebridge install: copied $src_path -> $dst_path"

# On Windows also copy the import library (.dll.lib) alongside the DLL so MSVC
# consumers can link against it, matching what a CMake SHARED library produces.
if ($target_os -eq 'win32') {
    $lib_src = Join-Path $release_dir 'NativeBridge.dll.lib'
    if (Test-Path $lib_src -PathType Leaf) {
        Copy-Item -Path $lib_src -Destination (Join-Path $dst_dir 'NativeBridge.lib') -Force
        Write-Host "nativebridge install: copied import lib"
    }
    $pdb_src = Join-Path $release_dir 'NativeBridge.pdb'
    if (Test-Path $pdb_src -PathType Leaf) {
        Copy-Item -Path $pdb_src -Destination (Join-Path $dst_dir 'NativeBridge.pdb') -Force
    }
}

# clean1.ps1 — trim the nativebridge install tree to just the artifacts Unity
# needs (the native lib + public headers), then strip symbols to shrink the
# shipped binary. No bin/curl-config, cmake or pkgconfig is produced.
#
# The whole native library statically bundles curl + BoringSSL + nghttp2/3 +
# ngtcp2 + zlib, so it carries a huge local symbol table. Stripping it (together
# with the -fvisibility=hidden + link-time gc-sections/dead_strip applied in
# native/CMakeLists.txt) removes most of that dead weight without touching the
# exported API Unity calls.
$install_dir = $args[0]

function sremove($path) {
    if (Test-Path $path) { Remove-Item $path -Recurse -Force }
}

# Run a strip-like tool, preferring the LLVM strip that ships with the NDK/Xcode
# toolchains; fall back to the system strip. Missing tools are non-fatal.
function invoke_strip($tool, $stripArgs, $file) {
    $prog = Get-Command $tool -ErrorAction SilentlyContinue
    if (!$prog) { return $false }
    $before = (Get-Item $file).Length
    & $prog.Source @stripArgs $file 2>$null
    if ($LASTEXITCODE -eq 0) {
        $after = (Get-Item $file).Length
        Write-Output ("nativebridge clean: stripped {0} ({1:N0} -> {2:N0} bytes)" -f (Split-Path $file -Leaf), $before, $after)
        return $true
    }
    return $false
}

function strip_file($file) {
    $ext = [System.IO.Path]::GetExtension($file)
    # Prefer llvm-strip (NDK/Xcode) then the platform strip.
    $tools = @('llvm-strip', 'strip')
    foreach ($t in $tools) {
        if ($ext -eq '.a') {
            # Static archive (iOS/tvOS): keep GLOBAL symbols — IL2CPP still links
            # against them later — and only remove local/debug symbols. `-x` does
            # exactly that and preserves the archive's symbol index.
            if (invoke_strip $t @('-x') $file) { return }
        }
        else {
            # Shared lib (.so/.dylib): drop everything not needed at load time.
            if (invoke_strip $t @('--strip-unneeded') $file) { return }
            # macOS strip doesn't accept --strip-unneeded; retry with -x.
            if (invoke_strip $t @('-x') $file) { return }
        }
    }
    Write-Output "nativebridge clean: no usable strip tool for $(Split-Path $file -Leaf), skipped"
}

if ((Test-Path $install_dir -PathType Container)) {
    Write-Output "Cleaning ${install_dir}..."
    sremove "$install_dir/share"
    sremove "$install_dir/lib/cmake"
    sremove "$install_dir/lib/pkgconfig"

    # Strip the native libraries. Windows .dll/.lib carry no symbol table (debug
    # info lives in a separate .pdb we don't ship), so there's nothing to strip
    # there; the loop simply finds no .so/.a/.dylib on Windows.
    foreach ($sub in @('lib', 'bin')) {
        $dir = Join-Path $install_dir $sub
        if (Test-Path $dir -PathType Container) {
            Get-ChildItem $dir -File -Include '*.so', '*.dylib', '*.a' -Recurse | ForEach-Object {
                strip_file $_.FullName
            }
        }
    }
}

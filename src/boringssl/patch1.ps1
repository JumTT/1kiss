#!/usr/bin/env pwsh
param(
    $lib_src,
    $ver
)

# BoringSSL unconditionally enables -Werror via add_compile_options() in its
# CMakeLists.txt. At recent dated tags it re-enabled Clang's -Wshorten-64-to-32,
# which turns benign 'long'->'int' narrowings (e.g. crypto/x509/x_crl.cc) into a
# hard error on LP64 targets (Linux/macOS clang). We only consume the static
# libs, so relax -Werror to keep the pinned release building across all archs.
# Note: -DCMAKE_C_FLAGS can't fix this, because add_compile_options() lands after
# CMAKE_*_FLAGS on the command line and -Werror would still win.

$cmakelists = Join-Path $lib_src 'CMakeLists.txt'
if (!(Test-Path $cmakelists -PathType Leaf)) {
    Write-Host "1kiss: BoringSSL CMakeLists.txt not found at $cmakelists, skip patch"
    return
}

$content = Get-Content $cmakelists -Raw

# Remove the standalone -Werror token from the C_CXX_WARNINGS list only.
$patched = $content -replace '(?m)^(\s*set\(C_CXX_WARNINGS\s+)-Werror\s+', '$1'

if ($patched -ne $content) {
    Set-Content -Path $cmakelists -Value $patched -NoNewline
    Write-Host "1kiss: patched BoringSSL CMakeLists.txt to drop -Werror"
}
elseif ($content -notmatch '(?m)^\s*set\(C_CXX_WARNINGS\b') {
    Write-Host "1kiss: WARNING - C_CXX_WARNINGS not found in BoringSSL CMakeLists.txt (upstream layout changed?)"
}
else {
    Write-Host "1kiss: BoringSSL -Werror already absent, nothing to patch"
}

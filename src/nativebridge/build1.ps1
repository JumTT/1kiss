# build1.ps1 — build NativeBridge (Rust cdylib) with cargo.
#
# Args: $target_os $target_cpu $install_dir
# Invoked by build.ps1 from the staged source dir ($lib_src), after all
# dependency install_dirs are exposed as $<lib>_install_dir globals.

$target_os = $args[0]
$target_cpu = $args[1]
$install_dir = $args[2]

function fail($msg) { throw "nativebridge build: $msg" }

Write-Host "nativebridge build1: target_os=$target_os target_cpu=$target_cpu"
Write-Host "  install_dir=$install_dir"

# The 1k build system tracks a single "apple embedded simulator" flag
# ($Global:is_ios_sim, set in 1k/1kiss.ps1 from -sdk simulator or an x64 arch)
# that is true for BOTH iOS and tvOS simulator builds; there is no separate
# is_tvos_sim. Reuse this one flag for every apple embedded target.
$is_apple_sim = [bool]$Global:is_ios_sim

$rust_target = $null
if ($target_os -eq 'win32') {
    if ($target_cpu -eq 'x64' -or $target_cpu -eq 'amd64') {
        $rust_target = 'x86_64-pc-windows-msvc'
    } elseif ($target_cpu -eq 'arm64') {
        $rust_target = 'aarch64-pc-windows-msvc'
    } else { fail "unsupported windows cpu: $target_cpu" }
} elseif ($target_os -eq 'linux') {
    if ($target_cpu -eq 'x64') {
        $rust_target = 'x86_64-unknown-linux-gnu'
    } elseif ($target_cpu -eq 'arm64') {
        $rust_target = 'aarch64-unknown-linux-gnu'
    } else { fail "unsupported linux cpu: $target_cpu" }
} elseif ($target_os -eq 'android') {
    if ($target_cpu -eq 'armv7') {
        $rust_target = 'armv7-linux-androideabi'
    } elseif ($target_cpu -eq 'arm64') {
        $rust_target = 'aarch64-linux-android'
    } elseif ($target_cpu -eq 'x86') {
        $rust_target = 'i686-linux-android'
    } elseif ($target_cpu -eq 'x64') {
        $rust_target = 'x86_64-linux-android'
    } else { fail "unsupported android cpu: $target_cpu" }
} elseif ($target_os -eq 'osx') {
    if ($target_cpu -eq 'x64') {
        $rust_target = 'x86_64-apple-darwin'
    } elseif ($target_cpu -eq 'arm64') {
        $rust_target = 'aarch64-apple-darwin'
    } else { fail "unsupported osx cpu: $target_cpu" }
} elseif ($target_os -eq 'ios') {
    if ($is_apple_sim) {
        if ($target_cpu -eq 'arm64') {
            $rust_target = 'aarch64-apple-ios-sim'
        } elseif ($target_cpu -eq 'x64') {
            $rust_target = 'x86_64-apple-ios'
        } else { fail "unsupported ios-sim cpu: $target_cpu" }
    } else {
        if ($target_cpu -eq 'arm64') {
            $rust_target = 'aarch64-apple-ios'
        } else { fail "ios device only supports arm64, got: $target_cpu" }
    }
} elseif ($target_os -eq 'tvos') {
    if ($is_apple_sim) {
        if ($target_cpu -eq 'arm64') {
            $rust_target = 'aarch64-apple-tvos-sim'
        } elseif ($target_cpu -eq 'x64') {
            $rust_target = 'x86_64-apple-tvos'
        } else { fail "unsupported tvos-sim cpu: $target_cpu" }
    } else {
        if ($target_cpu -eq 'arm64') {
            $rust_target = 'aarch64-apple-tvos'
        } else { fail "tvos device only supports arm64, got: $target_cpu" }
    }
} else {
    fail "unsupported target_os: $target_os"
}

Write-Host "  rust_target=$rust_target"

rustup target add $rust_target 2>&1 | Out-Null

$curl_dir = Get-Variable -Name 'curl_install_dir' -ValueOnly -ErrorAction SilentlyContinue
$boringssl_dir = Get-Variable -Name 'boringssl_install_dir' -ValueOnly -ErrorAction SilentlyContinue
$nghttp2_dir = Get-Variable -Name 'nghttp2_install_dir' -ValueOnly -ErrorAction SilentlyContinue
$nghttp3_dir = Get-Variable -Name 'nghttp3_install_dir' -ValueOnly -ErrorAction SilentlyContinue
$ngtcp2_dir = Get-Variable -Name 'ngtcp2_install_dir' -ValueOnly -ErrorAction SilentlyContinue
$zlib_dir = Get-Variable -Name 'zlib_install_dir' -ValueOnly -ErrorAction SilentlyContinue

if (-not $curl_dir) { fail 'curl_install_dir not set' }
if (-not $boringssl_dir) { fail 'boringssl_install_dir not set' }
if (-not $nghttp2_dir) { fail 'nghttp2_install_dir not set' }
if (-not $nghttp3_dir) { fail 'nghttp3_install_dir not set' }
if (-not $ngtcp2_dir) { fail 'ngtcp2_install_dir not set' }
if (-not $zlib_dir) { fail 'zlib_install_dir not set' }

if ($target_os -eq 'win32') {
    $env:NB_CURL_LIBRARY = "$curl_dir/lib/libcurl.lib"
    $env:NB_SSL_LIBRARY = "$boringssl_dir/lib/ssl.lib"
    $env:NB_CRYPTO_LIBRARY = "$boringssl_dir/lib/crypto.lib"
    $env:NB_NGHTTP2_LIBRARY = "$nghttp2_dir/lib/nghttp2.lib"
    $env:NB_NGHTTP3_LIBRARY = "$nghttp3_dir/lib/nghttp3.lib"
    $env:NB_NGTCP2_LIBRARY = "$ngtcp2_dir/lib/ngtcp2.lib"
    $env:NB_NGTCP2_CRYPTO_LIBRARY = "$ngtcp2_dir/lib/ngtcp2_crypto_boringssl.lib"
    $env:NB_ZLIB_LIBRARY = "$zlib_dir/lib/zs.lib"
} else {
    $env:NB_CURL_LIBRARY = "$curl_dir/lib/libcurl.a"
    $env:NB_SSL_LIBRARY = "$boringssl_dir/lib/libssl.a"
    $env:NB_CRYPTO_LIBRARY = "$boringssl_dir/lib/libcrypto.a"
    $env:NB_NGHTTP2_LIBRARY = "$nghttp2_dir/lib/libnghttp2.a"
    $env:NB_NGHTTP3_LIBRARY = "$nghttp3_dir/lib/libnghttp3.a"
    $env:NB_NGTCP2_LIBRARY = "$ngtcp2_dir/lib/libngtcp2.a"
    $env:NB_NGTCP2_CRYPTO_LIBRARY = "$ngtcp2_dir/lib/libngtcp2_crypto_boringssl.a"
    $env:NB_ZLIB_LIBRARY = "$zlib_dir/lib/libz.a"
}

$env:NB_LIB_DIRS = @(
    "$curl_dir/lib",
    "$boringssl_dir/lib",
    "$nghttp2_dir/lib",
    "$nghttp3_dir/lib",
    "$ngtcp2_dir/lib",
    "$zlib_dir/lib"
) -join ';'

Write-Host "  NB_CURL_LIBRARY=$env:NB_CURL_LIBRARY"
Write-Host "  NB_SSL_LIBRARY=$env:NB_SSL_LIBRARY"
Write-Host "  NB_NGHTTP2_LIBRARY=$env:NB_NGHTTP2_LIBRARY"

if ($target_os -eq 'android') {
    # Rust must link Android targets with the NDK's per-API clang driver, not the
    # host `cc`. Otherwise the final link fails with e.g.
    #   ld: error: unable to find library -llog / -landroid / -lc++ / -lunwind
    # because the host linker has no NDK sysroot. active_ndk_toolchain() (from
    # build.ps1) put the NDK bin dir on PATH and exposed $env:ANDROID_NDK_BIN;
    # $Global:android_api_level carries the per-arch API level.
    if (-not $env:ANDROID_NDK_BIN) { fail 'ANDROID_NDK_BIN not set (NDK toolchain not activated)' }
    $api = $Global:android_api_level
    if (-not $api) { fail 'android_api_level not set' }
    $clang_prefix = switch ($target_cpu) {
        'arm64' { 'aarch64-linux-android' }
        'armv7' { 'armv7a-linux-androideabi' }
        'x86' { 'i686-linux-android' }
        'x64' { 'x86_64-linux-android' }
        default { fail "unsupported android cpu: $target_cpu" }
    }
    $clang_name = "$clang_prefix$api-clang"
    if ($IsWindows) { $clang_name = "$clang_name.cmd" }
    $android_linker = Join-Path $env:ANDROID_NDK_BIN $clang_name
    if (-not (Test-Path $android_linker -PathType Leaf)) { fail "android linker not found: $android_linker" }
    # cargo derives this var name from the target triple (upper-cased, '-' -> '_').
    $linker_var = 'CARGO_TARGET_' + ($rust_target.ToUpper() -replace '-', '_') + '_LINKER'
    Set-Item -Path "env:$linker_var" -Value $android_linker
    Write-Host "  android_linker=$android_linker ($linker_var)"
}
elseif ($target_os -eq 'ios' -or $target_os -eq 'tvos' -or $target_os -eq 'watchos') {
    # The dependency C libraries (curl, boringssl, ...) are compiled by
    # 1k/ios.cmake with a specific deployment target (tvOS=15.0, iOS=11/12,
    # watchOS=8.0). Rust's builtin apple targets otherwise default to a much
    # older min-OS (e.g. tvOS 10.0), so the final link picks the wrong platform
    # stub of libSystem and fails, e.g.:
    #   Undefined symbols: ___chkstk_darwin   (referenced by tvOS 15 objects)
    # Force rustc to link with the SAME min-OS via *_DEPLOYMENT_TARGET, mirroring
    # 1k/ios.cmake (honoring an explicit -minsdk override when present).
    $deploy = $Global:target_minsdk
    if (-not $deploy) {
        if ($target_os -eq 'ios') {
            if ($target_cpu -eq 'armv7') {
                $deploy = '10.0'
            }
            else {
                $xcv = ($Global:XCODE_VERSION -replace '[^0-9.].*$', '') -split '\.'
                $xc_major = [int]$xcv[0]
                $xc_minor = if ($xcv.Count -gt 1) { [int]$xcv[1] } else { 0 }
                # xcode 14.3+ requires iOS 12.0 (c++ std::get); older uses 11.0.
                $deploy = if (($xc_major -gt 14) -or ($xc_major -eq 14 -and $xc_minor -ge 3)) { '12.0' } else { '11.0' }
            }
        }
        elseif ($target_os -eq 'tvos') {
            $deploy = '15.0'
        }
        elseif ($target_os -eq 'watchos') {
            $deploy = '8.0'
        }
    }
    if ($deploy) {
        switch ($target_os) {
            'ios' { $env:IPHONEOS_DEPLOYMENT_TARGET = $deploy }
            'tvos' { $env:TVOS_DEPLOYMENT_TARGET = $deploy }
            'watchos' { $env:WATCHOS_DEPLOYMENT_TARGET = $deploy }
        }
        Write-Host "  deployment_target=$deploy"
    }
}

Write-Host "nativebridge build1: running cargo build --release --target $rust_target"
cargo build --release --target $rust_target
if ($LASTEXITCODE -ne 0) { fail "cargo build failed with exit code $LASTEXITCODE" }

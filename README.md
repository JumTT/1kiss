# 1kiss (1k)

A cross-platform one-click build tool written in PowerShell thats support auto setup general dependent tools(cmake,google gn,ndk,android-sdk,emsdk,ninja,llvm,msvc,xcodebuild,emsdk,...)  
*The cross-platform build become so easy if you're using 1k!*

## Best practice：[Axmol](https://axmol.dev)

for example, if you use git to clone axmol(~80MB) and run it's `setup.ps1`, then goto root directory of your cmake based project and run:

`axmol -p android -a arm64` can build android on any host OS(macOS, Linux, Windows)

## Used by

- [yasio](https://github.com/yasio/yasio)
  
[![Release](https://img.shields.io/github/v/release/simdsoft/1kiss?include_prereleases&label=release)](../../releases/latest)
[![build](https://github.com/simdsoft/1kiss/actions/workflows/build.yml/badge.svg)](https://github.com/simdsoft/1kiss/actions/workflows/build.yml)
[![dist](https://github.com/simdsoft/1kiss/actions/workflows/dist.yml/badge.svg)](https://github.com/simdsoft/1kiss/actions/workflows/dist.yml)
[![Downloads](https://img.shields.io/github/downloads/simdsoft/1kiss/total.svg?label=downloads&colorB=orange)](../../releases/latest)

## OSS

- [![zlib](https://img.shields.io/badge/zlib-green.svg)](https://github.com/madler/zlib)
- [![boringssl](https://img.shields.io/badge/boringssl-green.svg)](https://github.com/google/boringssl) - Google's TLS stack, replaces OpenSSL
- [![nghttp2](https://img.shields.io/badge/nghttp2-green.svg)](https://github.com/nghttp2/nghttp2) - HTTP/2
- [![nghttp3](https://img.shields.io/badge/nghttp3-green.svg)](https://github.com/ngtcp2/nghttp3) - HTTP/3
- [![ngtcp2](https://img.shields.io/badge/ngtcp2-green.svg)](https://github.com/ngtcp2/ngtcp2) - QUIC (BoringSSL crypto backend)
- [![curl](https://img.shields.io/badge/curl-green.svg)](https://github.com/curl/curl/releases) - HTTP/1.1 + HTTP/2 + HTTP/3

## Notes

- *Since v81, use xcframework for apple platforms*

## Build Targets:

- osx: 
  - arm64 (M1+)
  - x86_64
- linux: 
  - x86_64
  - arm64 (since v114)
- ios:
  - arm64
  - arm64 simulator
  - x86_64 simulator
- tvos:
  - arm64
  - arm64 simulator
  - x86_64 simulator
- android
  - armv7
  - arm64
  - x86 (`DEPRECATED`)
  - x86_64
- win32 (Windows Desktop Apps)
  - x86 (`DEPRECATED`)
  - x86_64
  - arm64 (since v115)
- winrt/winuwp (Windows Universal Apps)
  - x86_64
  - arm64

## refers

- cppwinrt: Since axmol-2.1-`LTS`: migrate from `C++/CX` to [`cppwinrt`](https://learn.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/move-to-winrt-from-wrl) for breaking c++ standard limition, benefits to use c++20 on all target platforms.
- chrome releases: https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1

## NativeBridge (Unity / C#)

`NativeBridge` is an aggregate native library for Unity: it links the curl HTTP/2+HTTP/3 stack (curl + BoringSSL + nghttp2/nghttp3/ngtcp2 + zlib) that 1kiss builds into a **single self-contained native library** per platform, and exposes a P/Invoke-friendly C ABI (`curlw_*`) plus a matching `Curlw.cs`. `curlw` is the first feature module; more can be added under `src/nativebridge/native/modules/`.

### Layout

- `src/nativebridge/native/` — C/C++ sources: `modules/curlw/{curlw.h,curlw.cpp}` (the authoritative ABI + impl), `compat/` (yasio-free socket shim), `nativebridge.{h,cpp}`, `CMakeLists.txt`.
- `src/nativebridge/csharp/` — `Curlw.cs` (namespace `NativeBridge`) + `NativeBridge.asmdef`.
- `src/nativebridge/build.yml` — `local: true` module, built last in the chain.

### Versioning

- SemVer, single source of truth = `src/nativebridge/build.yml` `ver:`. `curlw.h` carries `CURLW_ABI_VERSION` (bump on any signature/layout change).
- Locked components: curl 8.21.0 / boringssl 0.20260803.0 / nghttp2 1.70.0 / nghttp3 1.18.0 / ngtcp2 1.25.0 / zlib 1.3.2.
- `nativebridge_version()` returns a runtime string of the library + component versions.

### Platform → product → DllImport name

| Unity platform | product | `LIBNAME` |
| --- | --- | --- |
| Windows x64 / arm64 | `NativeBridge.dll` | `NativeBridge` |
| Android arm64-v8a / armeabi-v7a / x86_64 / x86 | `libNativeBridge.so` | `NativeBridge` |
| iOS / tvOS (device + sim) | `NativeBridge.xcframework` | `__Internal` |
| macOS (universal) | `NativeBridge.dylib` | `NativeBridge` |
| Linux x64 / arm64 | `libNativeBridge.so` | `NativeBridge` |
| WebGL | — (unsupported: needs raw sockets/UDP) | — |

### Build

```powershell
# one platform, locally (curl is forced static via NATIVEBRIDGE=1):
$env:NATIVEBRIDGE=1
.\build.ps1 -p win32 -a x64 -libs 'zlib,boringssl,nghttp3,ngtcp2,nghttp2,curl,nativebridge'
# or: ./src/nativebridge/pack.ps1
```

CI: run the `nativebridge` workflow (`.github/workflows/nativebridge.yml`) to build every platform and produce `NativeBridge-CSharpe-<ver>.zip` (a drop-in Unity package with `Plugins/*` + `Runtime/Curlw.cs`).

> Native callbacks (`CurlwWriteDataDelegate`, `CurlwSocketManagedDelegate`) must be `static` methods decorated with `[MonoPInvokeCallback]` on IL2CPP/AOT targets. `curlw_perform` blocks — call it off the Unity main thread.

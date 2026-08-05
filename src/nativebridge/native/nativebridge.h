// nativebridge.h — library-level export macro + core API for the NativeBridge
// aggregate native library (Unity/C# consumption).
//
// NativeBridge bundles independent feature "modules" (curlw is the first) into
// one shared library so a Unity project only ships/loads a single native lib
// per platform. Every exported C entry point is decorated with NATIVEBRIDGE_API
// and uses the C calling convention (cdecl) to match [DllImport] on the C# side.
#ifndef NATIVEBRIDGE_H
#define NATIVEBRIDGE_H

// --- Export / visibility -----------------------------------------------------
#if defined(_WIN32)
#  if defined(NATIVEBRIDGE_BUILD_AS_DLL)
#    if defined(NATIVEBRIDGE_LIB)
#      define NATIVEBRIDGE_API __declspec(dllexport)
#    else
#      define NATIVEBRIDGE_API __declspec(dllimport)
#    endif
#  else
#    define NATIVEBRIDGE_API
#  endif
#else
#  if defined(NATIVEBRIDGE_LIB)
#    define NATIVEBRIDGE_API __attribute__((visibility("default")))
#  else
#    define NATIVEBRIDGE_API
#  endif
#endif

// --- Calling convention ------------------------------------------------------
// Fixed to cdecl so every exported function and every C# delegate marshalled as
// a native callback agrees with CallingConvention.Cdecl. Do NOT change without
// bumping the ABI major version.
#if defined(_WIN32)
#  define NATIVEBRIDGE_CALL __cdecl
#else
#  define NATIVEBRIDGE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns a human-readable version string describing this library and the
// versions of every bundled component, e.g.:
//   "NativeBridge 1.0.0 [curlw] (curl 8.21.0, boringssl ..., nghttp2 ..., ...)"
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL nativebridge_version(void);

#ifdef __cplusplus
}
#endif

#endif // NATIVEBRIDGE_H

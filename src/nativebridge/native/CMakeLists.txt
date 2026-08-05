# NativeBridge — Unity/C# aggregate native library.
#
# A container that links independent feature "modules" into one native library
# per platform. Platform support is decided PER MODULE, not for the whole
# library: e.g. curlw wraps libcurl (needs real sockets/UDP) so it is auto-
# disabled on Emscripten/wasm, but the container itself has no such restriction
# and future modules may support wasm.
#
# curlw is the first module; add more under modules/ later. Dependency lib paths
# arrive as NB_* cache vars from src/nativebridge/build.yml.
cmake_minimum_required(VERSION 3.16...3.31)

project(NativeBridge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED NB_VERSION)
  set(NB_VERSION "0.0.0")
endif()

# ---------------------------------------------------------------------------
# Platform detection (mirrors 1k/platforms.cmake; kept self-contained so this
# CMakeLists doesn't depend on that file being auto-included).
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME MATCHES "iOS")
  set(NB_IOS TRUE)
elseif(CMAKE_SYSTEM_NAME MATCHES "tvOS")
  set(NB_TVOS TRUE)
endif()
if(NB_IOS OR NB_TVOS)
  set(NB_APPLE_EMBED TRUE)  # iOS/tvOS => static lib for IL2CPP
endif()

# ---------------------------------------------------------------------------
# Feature modules
# ---------------------------------------------------------------------------
# curlw wraps libcurl, which needs sockets/UDP the wasm sandbox can't provide,
# so disable it there. This is a per-module decision; it does not gate the
# whole library.
option(NB_ENABLE_CURLW "Build the curlw (libcurl) feature module" ON)
if(EMSCRIPTEN)
  set(NB_ENABLE_CURLW OFF)
endif()

# ---------------------------------------------------------------------------
# Sources — the container core is always built; each module appends its own.
# ---------------------------------------------------------------------------
set(NB_SRC nativebridge.cpp)
set(NB_PUBLIC_HEADERS nativebridge.h)

if(NB_ENABLE_CURLW)
  list(APPEND NB_SRC modules/curlw/curlw.cpp)
  list(APPEND NB_PUBLIC_HEADERS modules/curlw/curlw.h)
endif()

# ---------------------------------------------------------------------------
# Target: static on iOS/tvOS, shared everywhere else.
# ---------------------------------------------------------------------------
if(NB_APPLE_EMBED)
  add_library(NativeBridge STATIC ${NB_SRC})
else()
  add_library(NativeBridge SHARED ${NB_SRC})
  target_compile_definitions(NativeBridge PRIVATE NATIVEBRIDGE_BUILD_AS_DLL=1)
endif()

set_target_properties(NativeBridge PROPERTIES OUTPUT_NAME "NativeBridge")

# Export our own symbols; keep everything else hidden so bundled deps don't leak.
target_compile_definitions(NativeBridge
  PRIVATE
    NATIVEBRIDGE_LIB=1
    NB_VERSION_STR="${NB_VERSION}"
)

if(NOT MSVC)
  target_compile_options(NativeBridge PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
endif()

target_include_directories(NativeBridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# curlw module: curl + deps includes/libs and the system libs they need.
# ---------------------------------------------------------------------------
if(NB_ENABLE_CURLW)
  target_compile_definitions(NativeBridge
    PRIVATE
      NB_HAVE_CURLW=1
      CURL_STATICLIB=1
      NGHTTP2_STATICLIB=1
      NGHTTP3_STATICLIB=1
      NGTCP2_STATICLIB=1
  )
  target_include_directories(NativeBridge
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/curlw
      ${NB_CURL_INCLUDE_DIR}
  )

  # Link order matters for GNU ld: curl -> ngtcp2(+crypto_boringssl) -> nghttp3
  # -> nghttp2 -> ssl -> crypto -> z. The --start-group below also guards against
  # any residual ordering/circular issues on ELF.
  set(NB_DEP_LIBS
    ${NB_CURL_LIBRARY}
    ${NB_NGTCP2_LIBRARY}
    ${NB_NGTCP2_CRYPTO_LIBRARY}
    ${NB_NGHTTP3_LIBRARY}
    ${NB_NGHTTP2_LIBRARY}
    ${NB_SSL_LIBRARY}
    ${NB_CRYPTO_LIBRARY}
    ${NB_ZLIB_LIBRARY}
  )
  if(CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android")
    # Wrap in a link group so archive ordering / circular refs resolve.
    target_link_libraries(NativeBridge PRIVATE -Wl,--start-group ${NB_DEP_LIBS} -Wl,--end-group)
  else()
    target_link_libraries(NativeBridge PRIVATE ${NB_DEP_LIBS})
  endif()

  # System libs curl/boringssl need per platform.
  if(WIN32)
    target_link_libraries(NativeBridge PRIVATE ws2_32 crypt32 bcrypt)
  elseif(CMAKE_SYSTEM_NAME MATCHES "Android")
    target_link_libraries(NativeBridge PRIVATE log android)
  elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(NativeBridge PRIVATE pthread dl)
  elseif(APPLE AND NOT NB_APPLE_EMBED)
    target_link_libraries(NativeBridge PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
  endif()
endif()

# ---------------------------------------------------------------------------
# Generic per-platform flags (independent of any module).
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME MATCHES "Android")
  # Google Play 16 KB page alignment.
  target_link_options(NativeBridge PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
endif()

# ---------------------------------------------------------------------------
# iOS/tvOS: merge dependency archives into libNativeBridge.a.
#
# On a STATIC target, target_link_libraries only records usage requirements — it
# does NOT copy curl/BoringSSL/nghttp*/ngtcp2/zlib objects into libNativeBridge.a.
# dist1.sh then wraps only libNativeBridge.a into the xcframework, so IL2CPP's
# final link would fail with unresolved curl_*/ssl_*/etc. symbols. Merge them
# here with `libtool -static` so the shipped archive is self-contained.
# ---------------------------------------------------------------------------
if(NB_APPLE_EMBED AND NB_ENABLE_CURLW)
  set(NB_MERGED_LIB "${CMAKE_CURRENT_BINARY_DIR}/libNativeBridge.merged.a")
  add_custom_command(TARGET NativeBridge POST_BUILD
    COMMAND libtool -static -o "${NB_MERGED_LIB}"
            "$<TARGET_FILE:NativeBridge>"
            ${NB_DEP_LIBS}
    COMMAND ${CMAKE_COMMAND} -E copy "${NB_MERGED_LIB}" "$<TARGET_FILE:NativeBridge>"
    COMMENT "Merging curl + deps into libNativeBridge.a (iOS/tvOS self-contained archive)"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Install into install_.../nativebridge/{lib,bin,include}
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS NativeBridge
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/nativebridge.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(NB_ENABLE_CURLW)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/modules/curlw/curlw.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

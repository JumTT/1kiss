// nativebridge.cpp — library-level entry points.
//
// The container core has no hard dependency on any single module: module-
// specific details (e.g. curl versions) are compiled in only when that module
// is enabled (NB_HAVE_CURLW), so the library still builds if a module is off.
#include "nativebridge.h"
#include <cstdio>

#if defined(NB_HAVE_CURLW)
#  include <curl/curl.h>
#endif

#ifndef NB_VERSION_STR
#  define NB_VERSION_STR "0.0.0"
#endif

extern "C" {

NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL nativebridge_version(void)
{
    // Built once. Lists this library's version, the enabled modules, and (when
    // the curlw module is present) the underlying curl/ssl/http2 versions curl
    // reports at runtime.
    static char buf[512];
    static bool built = false;
    if (!built)
    {
#if defined(NB_HAVE_CURLW)
        curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
        snprintf(buf, sizeof(buf),
                 "NativeBridge %s [curlw] (curl %s, ssl %s, nghttp2 %s)",
                 NB_VERSION_STR,
                 v && v->version ? v->version : "?",
                 v && v->ssl_version ? v->ssl_version : "?",
                 v && v->nghttp2_version ? v->nghttp2_version : "?");
#else
        snprintf(buf, sizeof(buf), "NativeBridge %s []", NB_VERSION_STR);
#endif
        built = true;
    }
    return buf;
}

} // extern "C"

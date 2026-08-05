// curlw.h — authoritative C ABI for the "curlw" module (curl wrapper).
//
// This header is the SINGLE SOURCE OF TRUTH for the native<->C# contract.
// Curlw.cs mirrors these declarations one-for-one. Every function:
//   * is extern "C" (no name mangling),
//   * is decorated with NATIVEBRIDGE_API (exported),
//   * uses NATIVEBRIDGE_CALL == cdecl (matches CallingConvention.Cdecl in C#).
//
// Rationale for the wrapper: libcurl's curl_easy_setopt/getinfo are variadic,
// which P/Invoke marshals unreliably. curlw exposes fixed-arity, typed variants
// (int / long / off_t / pointer / string) plus handle-based easy & multi APIs.
//
// ABI versioning: bump CURLW_ABI_VERSION whenever an existing signature or a
// struct layout changes (a C# breaking change). Purely additive exports do not
// bump it.
#ifndef CURLW_H
#define CURLW_H

#include "nativebridge.h"
#include <stdint.h>
#include <curl/curl.h>

#define CURLW_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// --- version / ABI -----------------------------------------------------------
NATIVEBRIDGE_API int         NATIVEBRIDGE_CALL curlw_abi_version(void);
// Raw curl_version() string (kept for parity with the reference binding).
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_version_imp(void);

// --- raw socket helpers ------------------------------------------------------
// intptr_t carries a native socket handle across the ABI (SOCKET on Win64 is
// 64-bit; int on POSIX). C# marshals these as IntPtr.
NATIVEBRIDGE_API intptr_t NATIVEBRIDGE_CALL curlw_create_socket(int af, int type, int protocol);
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL curlw_shutdown_socket(intptr_t sockfd);
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL curlw_close_socket(intptr_t sockfd);
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL curlw_errno(void);

// --- global init / cleanup ---------------------------------------------------
// max_fd_set sizes the internal fd_set object pool chunk.
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_global_init(int flags, unsigned int max_fd_set);
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_global_cleanup(void);

// --- fd_set pool + select ----------------------------------------------------
NATIVEBRIDGE_API fd_set*  NATIVEBRIDGE_CALL curlw_socket_allocfds(void);
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_socket_freefds(fd_set* pfds);
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_socket_zerofds(fd_set* pfds);
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL curlw_socket_select(int nfds, fd_set* readfds,
                                                               fd_set* writefds, fd_set* exceptfds,
                                                               uint64_t microseconds);

// --- easy API ----------------------------------------------------------------
NATIVEBRIDGE_API CURL*       NATIVEBRIDGE_CALL curlw_easy_init(void);
NATIVEBRIDGE_API CURLcode    NATIVEBRIDGE_CALL curlw_easy_perform(CURL* handle);
NATIVEBRIDGE_API void        NATIVEBRIDGE_CALL curlw_easy_cleanup(CURL* handle);
NATIVEBRIDGE_API void        NATIVEBRIDGE_CALL curlw_easy_reset(CURL* handle);
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_easy_strerror_imp(CURLcode error);

// setopt: one typed variant per curl option class.
//   _int    -> CURLOPTTYPE_LONG options that fit in 32 bits
//   _long   -> CURLOPTTYPE_LONG options. The parameter is int64_t (a fixed width
//              that matches C#'s always-64-bit `long`), then narrowed to curl's
//              native `long` inside. Using a plain C `long` here would be 32-bit
//              on Win64 (LLP64) yet 64-bit in C#, so the marshalled argument
//              widths would disagree — hence the explicit int64_t.
//   _offt   -> CURLOPTTYPE_OFF_T options (curl_off_t, 64-bit)
//   _pointer/_string -> pointer/string options
// NOTE: the reference binding conflated _long with off_t; here they are split.
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_int(CURL* handle, CURLoption option, int optval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_long(CURL* handle, CURLoption option, int64_t optval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_offt(CURL* handle, CURLoption option, int64_t optval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_pointer(CURL* handle, CURLoption option, void* optval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_string(CURL* handle, CURLoption option, const char* optval);

// getinfo: typed variants.
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_int(CURL* handle, CURLINFO info, int* outval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_long(CURL* handle, CURLINFO info, int64_t* outval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_double(CURL* handle, CURLINFO info, double* outval);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_pointer(CURL* handle, CURLINFO info, void** outval);

// --- open/close socket callbacks --------------------------------------------
// The C# side registers ONE process-global callback of this signature; the
// per-handle *_set_* functions then wire curl's CURLOPT_*SOCKETFUNCTION to an
// internal trampoline that forwards to it. Signature: returns non-zero to let
// the callback take responsibility, sockfd carried as intptr_t.
typedef int (NATIVEBRIDGE_CALL* curlw_socket_managed_cb)(intptr_t sockfd, void* userptr);

NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_easy_set_opensocket_global_cb(curlw_socket_managed_cb cb);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_set_opensocket_cb(CURL* handle, void* userdata);
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_easy_clear_opensocket_cb(CURL* handle);

NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_easy_set_closesocket_global_cb(curlw_socket_managed_cb cb);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_set_closesocket_cb(CURL* handle, void* userdata);
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL curlw_easy_clear_closesocket_cb(CURL* handle);

// --- multi API ---------------------------------------------------------------
NATIVEBRIDGE_API CURLM*      NATIVEBRIDGE_CALL curlw_multi_init(void);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_cleanup(CURLM* multi_handle);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_add_handle(CURLM* multi_handle, CURL* easy_handle);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_remove_handle(CURLM* multi_handle, CURL* easy_handle);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_perform(CURLM* multi_handle, int* running_handles);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_fdset(CURLM* multi_handle, fd_set* read_fds,
                                                               fd_set* write_fds, fd_set* exc_fds, int* max_fd);
// curl_multi_timeout uses `long`; normalized to int64 for stable cross-platform width.
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_timeout(CURLM* multi_handle, int64_t* milliseconds);
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_multi_strerror_imp(CURLMcode error);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_setopt_int(CURLM* multi_handle, CURLMoption option, int optval);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_setopt_long(CURLM* multi_handle, CURLMoption option, int64_t optval);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_setopt_offt(CURLM* multi_handle, CURLMoption option, int64_t optval);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_setopt_pointer(CURLM* multi_handle, CURLMoption option, void* optval);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_setopt_string(CURLM* multi_handle, CURLMoption option, const char* optval);

// info_read returns a pointer into curl-owned memory (valid until the next
// curl_multi call). Rather than force C# to mirror CURLMsg's exact layout, use
// the accessor functions below to read its fields safely across ABIs.
NATIVEBRIDGE_API CURLMsg*  NATIVEBRIDGE_CALL curlw_multi_info_read(CURLM* multi_handle, int* msgs_in_queue);
NATIVEBRIDGE_API int       NATIVEBRIDGE_CALL curlw_msg_get_msg(const CURLMsg* msg);       // CURLMSG value
NATIVEBRIDGE_API CURL*     NATIVEBRIDGE_CALL curlw_msg_get_easy_handle(const CURLMsg* msg);
NATIVEBRIDGE_API CURLcode  NATIVEBRIDGE_CALL curlw_msg_get_result(const CURLMsg* msg);     // valid when msg==CURLMSG_DONE

// --- slist -------------------------------------------------------------------
NATIVEBRIDGE_API struct curl_slist* NATIVEBRIDGE_CALL curlw_slist_append(struct curl_slist* list, const char* value);
NATIVEBRIDGE_API void               NATIVEBRIDGE_CALL curlw_slist_free_all(struct curl_slist* list);

#ifdef __cplusplus
}
#endif

#endif // CURLW_H

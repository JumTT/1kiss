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
// BLOB options (CURLOPT_*_BLOB). Assembles a struct curl_blob{data,len,flags} and
// forwards it. Pass flags=CURL_BLOB_COPY (1) so libcurl copies the bytes and the
// caller's buffer need not outlive the call.
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_blob(CURL* handle, CURLoption option,
                                                                  void* data, size_t len, unsigned int flags);

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
// Event-driven driving. extra_fds may be NULL with extra_nfds==0. curl_multi_poll
// blocks (unlike wait) even when there are no fds, so it is the preferred pump.
// wakeup is thread-safe and breaks a blocking poll early.
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_poll(CURLM* multi_handle, struct curl_waitfd* extra_fds,
                                                              unsigned int extra_nfds, int timeout_ms, int* numfds);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_wait(CURLM* multi_handle, struct curl_waitfd* extra_fds,
                                                              unsigned int extra_nfds, int timeout_ms, int* numfds);
NATIVEBRIDGE_API CURLMcode   NATIVEBRIDGE_CALL curlw_multi_wakeup(CURLM* multi_handle);
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

// --- misc / memory -----------------------------------------------------------
// Frees memory allocated by libcurl (curl_easy_escape / curl_url_get /
// curl_multi_get_handles results). Always pair those with this.
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_free(void* p);

// --- easy: extra entry points ------------------------------------------------
NATIVEBRIDGE_API CURL*    NATIVEBRIDGE_CALL curlw_easy_duphandle(CURL* handle);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_pause(CURL* handle, int action);   // CURLPAUSE_*
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_upkeep(CURL* handle);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_recv(CURL* handle, void* buffer, size_t buflen, size_t* n);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_send(CURL* handle, const void* buffer, size_t buflen, size_t* n);
// Returns a curl-allocated string; free it with curlw_free.
NATIVEBRIDGE_API char*    NATIVEBRIDGE_CALL curlw_easy_escape(CURL* handle, const char* string, int length);
NATIVEBRIDGE_API char*    NATIVEBRIDGE_CALL curlw_easy_unescape(CURL* handle, const char* string, int inlength, int* outlength);

// --- version info ------------------------------------------------------------
// The struct is read via accessors (no C# struct-layout ABI). curlw_version_info
// returns a pointer to static curl-owned data (do not free).
NATIVEBRIDGE_API const curl_version_info_data* NATIVEBRIDGE_CALL curlw_version_info(void);
NATIVEBRIDGE_API int          NATIVEBRIDGE_CALL curlw_verinfo_features(const curl_version_info_data* d);      // CURL_VERSION_* bits
NATIVEBRIDGE_API unsigned int NATIVEBRIDGE_CALL curlw_verinfo_version_num(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_version(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_ssl_version(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_libz_version(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_nghttp2_version(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_quic_version(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_cainfo(const curl_version_info_data* d);
NATIVEBRIDGE_API const char*  NATIVEBRIDGE_CALL curlw_verinfo_capath(const curl_version_info_data* d);

// --- header API --------------------------------------------------------------
// curl_header is read via accessors. curlw_easy_header/nextheader return pointers
// into curl-owned memory (valid until the next easy call); do not free.
NATIVEBRIDGE_API CURLHcode NATIVEBRIDGE_CALL curlw_easy_header(CURL* handle, const char* name, size_t nameindex,
                                                              unsigned int origin, int request, struct curl_header** hout);
NATIVEBRIDGE_API struct curl_header* NATIVEBRIDGE_CALL curlw_easy_nextheader(CURL* handle, unsigned int origin,
                                                                            int request, struct curl_header* prev);
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_header_name(const struct curl_header* h);
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_header_value(const struct curl_header* h);
NATIVEBRIDGE_API size_t      NATIVEBRIDGE_CALL curlw_header_amount(const struct curl_header* h);
NATIVEBRIDGE_API size_t      NATIVEBRIDGE_CALL curlw_header_index(const struct curl_header* h);
NATIVEBRIDGE_API unsigned int NATIVEBRIDGE_CALL curlw_header_origin(const struct curl_header* h);

// --- share API ---------------------------------------------------------------
// Shares DNS / connection / TLS-session / cookie / HSTS / Alt-Svc caches across
// easy handles. Attach to an easy handle via CURLOPT_SHARE (setopt_pointer).
// For multi-threaded use call curlw_share_enable_default_locks: it installs an
// internal mutex-based lock/unlock so the share is thread-safe without the caller
// implementing lock callbacks.
NATIVEBRIDGE_API CURLSH*     NATIVEBRIDGE_CALL curlw_share_init(void);
NATIVEBRIDGE_API CURLSHcode  NATIVEBRIDGE_CALL curlw_share_cleanup(CURLSH* share);
NATIVEBRIDGE_API CURLSHcode  NATIVEBRIDGE_CALL curlw_share_setopt_int(CURLSH* share, CURLSHoption option, int value);
NATIVEBRIDGE_API CURLSHcode  NATIVEBRIDGE_CALL curlw_share_enable_default_locks(CURLSH* share);
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_share_strerror_imp(CURLSHcode error);

// --- MIME (multipart/form-data upload) ---------------------------------------
NATIVEBRIDGE_API curl_mime*     NATIVEBRIDGE_CALL curlw_mime_init(CURL* easy);
NATIVEBRIDGE_API void           NATIVEBRIDGE_CALL curlw_mime_free(curl_mime* mime);
NATIVEBRIDGE_API curl_mimepart* NATIVEBRIDGE_CALL curlw_mime_addpart(curl_mime* mime);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_name(curl_mimepart* part, const char* name);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_data(curl_mimepart* part, const char* data, size_t datasize);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_filedata(curl_mimepart* part, const char* filename);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_filename(curl_mimepart* part, const char* filename);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_type(curl_mimepart* part, const char* mimetype);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_encoder(curl_mimepart* part, const char* encoding);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_headers(curl_mimepart* part, struct curl_slist* headers, int take_ownership);
NATIVEBRIDGE_API CURLcode       NATIVEBRIDGE_CALL curlw_mime_subparts(curl_mimepart* part, curl_mime* subparts);

// --- URL API -----------------------------------------------------------------
// curl_url_get returns a curl-allocated string; free it with curlw_free.
NATIVEBRIDGE_API CURLU*      NATIVEBRIDGE_CALL curlw_url(void);
NATIVEBRIDGE_API void        NATIVEBRIDGE_CALL curlw_url_cleanup(CURLU* handle);
NATIVEBRIDGE_API CURLU*      NATIVEBRIDGE_CALL curlw_url_dup(const CURLU* in);
NATIVEBRIDGE_API CURLUcode   NATIVEBRIDGE_CALL curlw_url_get(const CURLU* handle, CURLUPart what, char** part, unsigned int flags);
NATIVEBRIDGE_API CURLUcode   NATIVEBRIDGE_CALL curlw_url_set(CURLU* handle, CURLUPart what, const char* part, unsigned int flags);
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_url_strerror_imp(CURLUcode error);

// --- WebSocket ---------------------------------------------------------------
// The frame metadata is read via accessors. meta pointers are curl-owned.
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_ws_recv(CURL* handle, void* buffer, size_t buflen,
                                                         size_t* recv, const struct curl_ws_frame** meta);
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_ws_send(CURL* handle, const void* buffer, size_t buflen,
                                                         size_t* sent, int64_t fragsize, unsigned int flags);
NATIVEBRIDGE_API const struct curl_ws_frame* NATIVEBRIDGE_CALL curlw_ws_meta(CURL* handle);
NATIVEBRIDGE_API int     NATIVEBRIDGE_CALL curlw_wsframe_flags(const struct curl_ws_frame* f);
NATIVEBRIDGE_API int64_t NATIVEBRIDGE_CALL curlw_wsframe_offset(const struct curl_ws_frame* f);
NATIVEBRIDGE_API int64_t NATIVEBRIDGE_CALL curlw_wsframe_bytesleft(const struct curl_ws_frame* f);
NATIVEBRIDGE_API size_t  NATIVEBRIDGE_CALL curlw_wsframe_len(const struct curl_ws_frame* f);

// --- multi: event-driven extensions ------------------------------------------
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_assign(CURLM* multi_handle, intptr_t sockfd, void* sockp);
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_socket_action(CURLM* multi_handle, intptr_t s,
                                                                      int ev_bitmask, int* running_handles);
// Returns a curl-allocated, NULL-terminated CURL* array; free it with curlw_free.
NATIVEBRIDGE_API CURL**    NATIVEBRIDGE_CALL curlw_multi_get_handles(CURLM* multi_handle);

#ifdef __cplusplus
}
#endif

#endif // CURLW_H

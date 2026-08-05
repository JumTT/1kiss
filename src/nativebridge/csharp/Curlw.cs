//
// Curlw.cs — C# binding for the NativeBridge "curlw" module.
//
// This is the managed mirror of the native contract declared in
//   src/nativebridge/native/modules/curlw/curlw.h
// Keep the two in lock-step: every P/Invoke below matches a NATIVEBRIDGE_API
// function 1:1 (cdecl calling convention, same argument widths).
//
// Rationale: libcurl's curl_easy_setopt / curl_easy_getinfo are variadic, which
// P/Invoke marshals unreliably. The native curlw layer exposes fixed-arity typed
// variants; this file exposes them to Unity.
//
// Design notes:
//   * namespace NativeBridge, LIBNAME "NativeBridge".
//   * CURLMsg is read via curlw_msg_* accessors, not a mirrored struct layout.
//   * setopt long vs off_t are distinct (curlw_easy_setopt_long / _offt).
//
#if !UNITY_WEBGL
using System;
using System.Runtime.InteropServices;
using CURLH = System.IntPtr;   // CURL*  (easy handle)
using CURLMH = System.IntPtr;  // CURLM* (multi handle)

namespace NativeBridge
{
    public static class CURLDefines
    {
        public const int CURLINFO_STRING = 0x100000;
        public const int CURLINFO_LONG = 0x200000;
        public const int CURLINFO_DOUBLE = 0x300000;
        public const int CURLINFO_SLIST = 0x400000;
        public const int CURLINFO_PTR = 0x400000; /* same as SLIST */
        public const int CURLINFO_SOCKET = 0x500000;
        public const int CURLINFO_OFF_T = 0x600000;
        public const int CURLINFO_MASK = 0x0fffff;
        public const int CURLINFO_TYPEMASK = 0xf00000;

        public const int CURLOPTTYPE_LONG = 0;
        public const int CURLOPTTYPE_OBJECTPOINT = 10000;
        public const int CURLOPTTYPE_FUNCTIONPOINT = 20000;
        public const int CURLOPTTYPE_OFF_T = 30000;
        public const int CURLOPTTYPE_BLOB = 40000;
        public const int CURLOPTTYPE_CBPOINT = CURLOPTTYPE_OBJECTPOINT;
        public const int CURLOPTTYPE_STRINGPOINT = CURLOPTTYPE_OBJECTPOINT;
        public const int CURLOPTTYPE_SLISTPOINT = CURLOPTTYPE_OBJECTPOINT;
        public const int CURLOPTTYPE_VALUES = CURLOPTTYPE_LONG;

        public const int CURL_GLOBAL_SSL = (1 << 0); /* no purpose since 7.57.0 */
        public const int CURL_GLOBAL_WIN32 = (1 << 1);
        public const int CURL_GLOBAL_ALL = (CURL_GLOBAL_SSL | CURL_GLOBAL_WIN32);
        public const int CURL_GLOBAL_NOTHING = 0;
        public const int CURL_GLOBAL_DEFAULT = CURL_GLOBAL_ALL;
        public const int CURL_GLOBAL_ACK_EINTR = (1 << 2);
    }

    /// <summary>
    /// Common curl options. Values verified against curl 8.21.0 include/curl/curl.h.
    /// Add more entries here as needed (copy the value from curl.h).
    /// </summary>
    public enum CURLoption
    {
        CURLOPT_URL                   = CURLDefines.CURLOPTTYPE_STRINGPOINT + 2,
        CURLOPT_PORT                  = CURLDefines.CURLOPTTYPE_LONG + 3,
        CURLOPT_RANGE                 = CURLDefines.CURLOPTTYPE_STRINGPOINT + 7,
        CURLOPT_VERBOSE               = CURLDefines.CURLOPTTYPE_LONG + 41,
        CURLOPT_HEADER                = CURLDefines.CURLOPTTYPE_LONG + 42,
        CURLOPT_NOPROGRESS            = CURLDefines.CURLOPTTYPE_LONG + 43,
        CURLOPT_NOBODY                = CURLDefines.CURLOPTTYPE_LONG + 44,
        CURLOPT_POST                  = CURLDefines.CURLOPTTYPE_LONG + 47,
        CURLOPT_FOLLOWLOCATION        = CURLDefines.CURLOPTTYPE_LONG + 52,
        // WARNING: libcurl does NOT copy CURLOPT_POSTFIELDS — it stores the
        // pointer and reads it later at perform() time. Do not set it via the
        // `string` overload (the marshalled buffer is freed when the P/Invoke
        // returns → dangling pointer). Use CURLOPT_COPYPOSTFIELDS (curl copies
        // the bytes), or pin/allocate a native buffer yourself and pass it via
        // the IntPtr overload, keeping it alive until the transfer completes.
        CURLOPT_POSTFIELDS            = CURLDefines.CURLOPTTYPE_OBJECTPOINT + 15,
        CURLOPT_COPYPOSTFIELDS        = CURLDefines.CURLOPTTYPE_OBJECTPOINT + 165,
        CURLOPT_USERAGENT             = CURLDefines.CURLOPTTYPE_STRINGPOINT + 18,
        CURLOPT_LOW_SPEED_LIMIT       = CURLDefines.CURLOPTTYPE_LONG + 19,
        CURLOPT_LOW_SPEED_TIME        = CURLDefines.CURLOPTTYPE_LONG + 20,
        CURLOPT_SSL_VERIFYPEER        = CURLDefines.CURLOPTTYPE_LONG + 64,
        CURLOPT_CAINFO                = CURLDefines.CURLOPTTYPE_STRINGPOINT + 65,
        CURLOPT_MAXCONNECTS           = CURLDefines.CURLOPTTYPE_LONG + 71,
        CURLOPT_POSTFIELDSIZE         = CURLDefines.CURLOPTTYPE_LONG + 60,
        CURLOPT_HTTPGET               = CURLDefines.CURLOPTTYPE_LONG + 80,
        CURLOPT_SSL_VERIFYHOST        = CURLDefines.CURLOPTTYPE_LONG + 81,
        CURLOPT_HTTP_VERSION          = CURLDefines.CURLOPTTYPE_VALUES + 84,
        CURLOPT_CUSTOMREQUEST         = CURLDefines.CURLOPTTYPE_STRINGPOINT + 36,
        CURLOPT_ACCEPT_ENCODING       = CURLDefines.CURLOPTTYPE_STRINGPOINT + 102,
        CURLOPT_BUFFERSIZE            = CURLDefines.CURLOPTTYPE_LONG + 98,
        CURLOPT_NOSIGNAL              = CURLDefines.CURLOPTTYPE_LONG + 99,
        CURLOPT_TIMEOUT_MS            = CURLDefines.CURLOPTTYPE_LONG + 155,
        CURLOPT_CONNECTTIMEOUT_MS     = CURLDefines.CURLOPTTYPE_LONG + 156,
        CURLOPT_TCP_KEEPALIVE         = CURLDefines.CURLOPTTYPE_LONG + 213,
        CURLOPT_TCP_KEEPIDLE          = CURLDefines.CURLOPTTYPE_LONG + 214,
        CURLOPT_TCP_KEEPINTVL         = CURLDefines.CURLOPTTYPE_LONG + 215,
        CURLOPT_MAX_RECV_SPEED_LARGE  = CURLDefines.CURLOPTTYPE_OFF_T + 146,
        CURLOPT_MAX_SEND_SPEED_LARGE  = CURLDefines.CURLOPTTYPE_OFF_T + 145,
        CURLOPT_INFILESIZE_LARGE      = CURLDefines.CURLOPTTYPE_OFF_T + 115,
        CURLOPT_POSTFIELDSIZE_LARGE   = CURLDefines.CURLOPTTYPE_OFF_T + 120,
        CURLOPT_HTTPHEADER            = CURLDefines.CURLOPTTYPE_SLISTPOINT + 23,
        CURLOPT_WRITEFUNCTION         = CURLDefines.CURLOPTTYPE_FUNCTIONPOINT + 11,
        CURLOPT_WRITEDATA             = CURLDefines.CURLOPTTYPE_CBPOINT + 1,
        CURLOPT_READFUNCTION          = CURLDefines.CURLOPTTYPE_FUNCTIONPOINT + 12,
        CURLOPT_READDATA              = CURLDefines.CURLOPTTYPE_CBPOINT + 9,
        CURLOPT_PRIVATE               = CURLDefines.CURLOPTTYPE_OBJECTPOINT + 103,
    }

    // https://curl.se/libcurl/c/CURLOPT_HTTP_VERSION.html
    public enum CURLhttpVersion
    {
        CURL_HTTP_VERSION_NONE,
        CURL_HTTP_VERSION_1_0,
        CURL_HTTP_VERSION_1_1,
        CURL_HTTP_VERSION_2_0,
        CURL_HTTP_VERSION_2TLS,
        CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE,
        CURL_HTTP_VERSION_3 = 30,      /* explicit HTTP/3, no fallback */
        CURL_HTTP_VERSION_3ONLY = 31,
    }

    public enum CURLcode
    {
        CURLE_OK = 0,
        CURLE_UNSUPPORTED_PROTOCOL,    /* 1 */
        CURLE_FAILED_INIT,             /* 2 */
        CURLE_URL_MALFORMAT,           /* 3 */
        CURLE_NOT_BUILT_IN,            /* 4 */
        CURLE_COULDNT_RESOLVE_PROXY,   /* 5 */
        CURLE_COULDNT_RESOLVE_HOST,    /* 6 */
        CURLE_COULDNT_CONNECT,         /* 7 */
        CURLE_WEIRD_SERVER_REPLY,      /* 8 */
        CURLE_REMOTE_ACCESS_DENIED,    /* 9 */
        CURLE_FTP_ACCEPT_FAILED,       /* 10 */
        CURLE_FTP_WEIRD_PASS_REPLY,    /* 11 */
        CURLE_FTP_ACCEPT_TIMEOUT,      /* 12 */
        CURLE_FTP_WEIRD_PASV_REPLY,    /* 13 */
        CURLE_FTP_WEIRD_227_FORMAT,    /* 14 */
        CURLE_FTP_CANT_GET_HOST,       /* 15 */
        CURLE_HTTP2,                   /* 16 */
        CURLE_FTP_COULDNT_SET_TYPE,    /* 17 */
        CURLE_PARTIAL_FILE,            /* 18 */
        CURLE_FTP_COULDNT_RETR_FILE,   /* 19 */
        CURLE_OBSOLETE20,              /* 20 */
        CURLE_QUOTE_ERROR,             /* 21 */
        CURLE_HTTP_RETURNED_ERROR,     /* 22 */
        CURLE_WRITE_ERROR,             /* 23 */
        CURLE_OBSOLETE24,              /* 24 */
        CURLE_UPLOAD_FAILED,           /* 25 */
        CURLE_READ_ERROR,              /* 26 */
        CURLE_OUT_OF_MEMORY,           /* 27 */
        CURLE_OPERATION_TIMEDOUT,      /* 28 */
        CURLE_OBSOLETE29,              /* 29 */
        CURLE_FTP_PORT_FAILED,         /* 30 */
        CURLE_FTP_COULDNT_USE_REST,    /* 31 */
        CURLE_OBSOLETE32,              /* 32 */
        CURLE_RANGE_ERROR,             /* 33 */
        CURLE_OBSOLETE34,              /* 34 (was CURLE_HTTP_POST_ERROR) */
        CURLE_SSL_CONNECT_ERROR,       /* 35 */
        CURLE_BAD_DOWNLOAD_RESUME,     /* 36 */
        CURLE_FILE_COULDNT_READ_FILE,  /* 37 */
        CURLE_LDAP_CANNOT_BIND,        /* 38 */
        CURLE_LDAP_SEARCH_FAILED,      /* 39 */
        CURLE_OBSOLETE40,              /* 40 */
        CURLE_OBSOLETE41,              /* 41 - NOT USED starting with 7.53.0 (was CURLE_FUNCTION_NOT_FOUND) */
        CURLE_ABORTED_BY_CALLBACK,     /* 42 */
        CURLE_BAD_FUNCTION_ARGUMENT,   /* 43 */
        CURLE_OBSOLETE44,              /* 44 */
        CURLE_INTERFACE_FAILED,        /* 45 */
        CURLE_OBSOLETE46,              /* 46 */
        CURLE_TOO_MANY_REDIRECTS,      /* 47 */
        CURLE_UNKNOWN_OPTION,          /* 48 */
        CURLE_SETOPT_OPTION_SYNTAX,    /* 49 */
        CURLE_OBSOLETE50,              /* 50 */
        CURLE_OBSOLETE51,              /* 51 */
        CURLE_GOT_NOTHING,             /* 52 */
        CURLE_SSL_ENGINE_NOTFOUND,     /* 53 */
        CURLE_SSL_ENGINE_SETFAILED,    /* 54 */
        CURLE_SEND_ERROR,              /* 55 */
        CURLE_RECV_ERROR,              /* 56 */
        CURLE_OBSOLETE57,              /* 57 */
        CURLE_SSL_CERTPROBLEM,         /* 58 */
        CURLE_SSL_CIPHER,              /* 59 */
        CURLE_PEER_FAILED_VERIFICATION,/* 60 */
        CURLE_BAD_CONTENT_ENCODING,    /* 61 */
        CURLE_OBSOLETE62,              /* 62 */
        CURLE_FILESIZE_EXCEEDED,       /* 63 */
        CURLE_USE_SSL_FAILED,          /* 64 */
        CURLE_SEND_FAIL_REWIND,        /* 65 */
        CURLE_SSL_ENGINE_INITFAILED,   /* 66 */
        CURLE_LOGIN_DENIED,            /* 67 */
        CURLE_TFTP_NOTFOUND,           /* 68 */
        CURLE_TFTP_PERM,               /* 69 */
        CURLE_REMOTE_DISK_FULL,        /* 70 */
        CURLE_TFTP_ILLEGAL,            /* 71 */
        CURLE_TFTP_UNKNOWNID,          /* 72 */
        CURLE_REMOTE_FILE_EXISTS,      /* 73 */
        CURLE_TFTP_NOSUCHUSER,         /* 74 */
        CURLE_OBSOLETE75,              /* 75 */
        CURLE_OBSOLETE76,              /* 76 */
        CURLE_SSL_CACERT_BADFILE,      /* 77 */
        CURLE_REMOTE_FILE_NOT_FOUND,   /* 78 */
        CURLE_SSH,                     /* 79 */
        CURLE_SSL_SHUTDOWN_FAILED,     /* 80 */
        CURLE_AGAIN,                   /* 81 */
        CURLE_SSL_CRL_BADFILE,         /* 82 */
        CURLE_SSL_ISSUER_ERROR,        /* 83 */
        CURLE_FTP_PRET_FAILED,         /* 84 */
        CURLE_RTSP_CSEQ_ERROR,         /* 85 */
        CURLE_RTSP_SESSION_ERROR,      /* 86 */
        CURLE_FTP_BAD_FILE_LIST,       /* 87 */
        CURLE_CHUNK_FAILED,            /* 88 */
        CURLE_NO_CONNECTION_AVAILABLE, /* 89 */
        CURLE_SSL_PINNEDPUBKEYNOTMATCH,/* 90 */
        CURLE_SSL_INVALIDCERTSTATUS,   /* 91 */
        CURLE_HTTP2_STREAM,            /* 92 */
        CURLE_RECURSIVE_API_CALL,      /* 93 */
        CURLE_AUTH_ERROR,              /* 94 */
        CURLE_HTTP3,                   /* 95 */
        CURLE_QUIC_CONNECT_ERROR,      /* 96 */
        CURLE_PROXY,                   /* 97 */
        CURLE_SSL_CLIENTCERT,          /* 98 */
        CURLE_UNRECOVERABLE_POLL,      /* 99 */
        CURLE_TOO_LARGE,               /* 100 */
        CURLE_ECH_REQUIRED,            /* 101 */
        CURL_LAST /* never use! */
    }

    public enum CURLMcode
    {
        CURLM_CALL_MULTI_PERFORM = -1,
        CURLM_OK,
        CURLM_BAD_HANDLE,
        CURLM_BAD_EASY_HANDLE,
        CURLM_OUT_OF_MEMORY,
        CURLM_INTERNAL_ERROR,
        CURLM_BAD_SOCKET,
        CURLM_UNKNOWN_OPTION,
        CURLM_ADDED_ALREADY,
        CURLM_RECURSIVE_API_CALL,
        CURLM_WAKEUP_FAILURE,
        CURLM_BAD_FUNCTION_ARGUMENT,
        CURLM_ABORTED_BY_CALLBACK,
        CURLM_UNRECOVERABLE_POLL,
        CURLM_LAST
    }

    public enum CURLINFO
    {
        CURLINFO_NONE = 0,
        CURLINFO_EFFECTIVE_URL   = CURLDefines.CURLINFO_STRING + 1,
        CURLINFO_RESPONSE_CODE   = CURLDefines.CURLINFO_LONG + 2,
        CURLINFO_TOTAL_TIME      = CURLDefines.CURLINFO_DOUBLE + 3,
        CURLINFO_NAMELOOKUP_TIME = CURLDefines.CURLINFO_DOUBLE + 4,
        CURLINFO_CONNECT_TIME    = CURLDefines.CURLINFO_DOUBLE + 5,
        CURLINFO_SIZE_DOWNLOAD_T = CURLDefines.CURLINFO_OFF_T + 8,
        CURLINFO_SPEED_DOWNLOAD_T = CURLDefines.CURLINFO_OFF_T + 9,
        CURLINFO_SIZE_UPLOAD_T   = CURLDefines.CURLINFO_OFF_T + 7,
        CURLINFO_CONTENT_LENGTH_DOWNLOAD_T = CURLDefines.CURLINFO_OFF_T + 15,
        CURLINFO_CONTENT_TYPE    = CURLDefines.CURLINFO_STRING + 18,
        CURLINFO_REDIRECT_URL    = CURLDefines.CURLINFO_STRING + 31,
        CURLINFO_PRIMARY_IP      = CURLDefines.CURLINFO_STRING + 32,
        CURLINFO_HTTP_VERSION    = CURLDefines.CURLINFO_LONG + 46,
        CURLINFO_TOTAL_TIME_T    = CURLDefines.CURLINFO_OFF_T + 50,
        CURLINFO_PRIVATE         = CURLDefines.CURLINFO_STRING + 21,
    }

    public enum CURLMSG
    {
        CURLMSG_NONE,
        CURLMSG_DONE, /* transfer complete; result carries the CURLcode */
        CURLMSG_LAST
    }

    /// <summary>
    /// Download data callback. size*nmemb bytes are available at <paramref name="content"/>.
    /// Return the number of bytes consumed; returning anything other than size*nmemb
    /// signals an error to curl. MUST be a static method decorated with
    /// [MonoPInvokeCallback(typeof(CurlwWriteDataDelegate))] for IL2CPP/AOT targets.
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate UIntPtr CurlwWriteDataDelegate(IntPtr content, UIntPtr size, UIntPtr nmemb, IntPtr userdata);

    /// <summary>
    /// Managed open/close socket callback. Matches native curlw_socket_managed_cb.
    /// For open: called first with sockfd == -1 (return non-zero to allow the
    /// socket, zero to refuse); then again with the created fd. For close: called
    /// with the fd to be closed (return non-zero if you took ownership of closing it).
    /// MUST be static + [MonoPInvokeCallback] on IL2CPP/AOT.
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int CurlwSocketManagedDelegate(IntPtr sockfd, IntPtr userptr);

    /// <summary>
    /// Thin 1:1 binding of the native curlw C ABI (see curlw.h). All entry points
    /// use cdecl to match NATIVEBRIDGE_CALL.
    /// </summary>
    public static class CurlwDLL
    {
#if (UNITY_IPHONE || UNITY_TVOS) && !UNITY_EDITOR
        public const string LIBNAME = "__Internal";
#else
        public const string LIBNAME = "NativeBridge";
#endif

        // --- version / ABI ---------------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int curlw_abi_version();

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_version_imp")]
        private static extern IntPtr curlw_version_imp();

        public static string curlw_version()
        {
            return Marshal.PtrToStringAnsi(curlw_version_imp());
        }

        // NativeBridge library-level version string.
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "nativebridge_version")]
        private static extern IntPtr nativebridge_version_imp();

        public static string nativebridge_version()
        {
            return Marshal.PtrToStringAnsi(nativebridge_version_imp());
        }

        // --- raw socket helpers ----------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr curlw_create_socket(int af, int type, int protocol);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int curlw_shutdown_socket(IntPtr sockfd);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int curlw_close_socket(IntPtr sockfd);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int curlw_errno();

        // --- global init / cleanup -------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_global_init(int flags, uint max_fd_set = 32);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void curlw_global_cleanup();

        // --- fd_set pool + select --------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr curlw_socket_allocfds();

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void curlw_socket_freefds(IntPtr pfds);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void curlw_socket_zerofds(IntPtr pfds);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int curlw_socket_select(int nfds, IntPtr readfds, IntPtr writefds,
                                                      IntPtr exceptfds, ulong microseconds);

        // --- easy API --------------------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLH curlw_easy_init();

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_perform(CURLH handle);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_easy_cleanup")]
        private static extern void curlw_easy_cleanup_imp(CURLH handle);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_easy_reset")]
        private static extern void curlw_easy_reset_imp(CURLH handle);

        // Managed wrappers release the per-handle callback strong references (set
        // via the delegate curlw_easy_setopt_pointer overload) so they can be GC'd
        // once curl no longer holds them.
        public static void curlw_easy_cleanup(CURLH handle)
        {
            curlw_easy_cleanup_imp(handle);
            ReleaseHandleCallbacks(handle);
        }

        public static void curlw_easy_reset(CURLH handle)
        {
            curlw_easy_reset_imp(handle);
            ReleaseHandleCallbacks(handle);
        }

        private static void ReleaseHandleCallbacks(IntPtr handle)
        {
            lock (s_cbLock)
            {
                s_handleCallbacks.Remove(handle);
            }
        }

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_easy_strerror_imp")]
        private static extern IntPtr curlw_easy_strerror_imp(CURLcode error);

        public static string curlw_easy_strerror(CURLcode error)
        {
            return Marshal.PtrToStringAnsi(curlw_easy_strerror_imp(error));
        }

        // setopt: typed variants (curl_easy_setopt is variadic; do NOT P/Invoke it directly)
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_setopt_int(CURLH handle, CURLoption option, int optval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_setopt_long(CURLH handle, CURLoption option, long optval);

        // For CURLOPTTYPE_OFF_T options only.
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_setopt_offt(CURLH handle, CURLoption option, long optval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_setopt_pointer(CURLH handle, CURLoption option, IntPtr optval);

        // Delegate overload: curl stores this function pointer on the easy handle
        // and invokes it during perform(). The managed delegate MUST stay alive
        // until the handle is cleaned up, else GC frees the thunk and the next
        // callback jumps into freed memory. This overload registers the delegate
        // in s_handleCallbacks; call curlw_easy_cleanup/reset (the managed
        // wrappers below) to release it.
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_easy_setopt_pointer")]
        private static extern CURLcode curlw_easy_setopt_pointer_cb_imp(CURLH handle, CURLoption option, CurlwWriteDataDelegate optval);

        // Per-handle strong references to callback delegates (write/read/...).
        private static readonly System.Collections.Generic.Dictionary<IntPtr, System.Collections.Generic.List<Delegate>>
            s_handleCallbacks = new System.Collections.Generic.Dictionary<IntPtr, System.Collections.Generic.List<Delegate>>();
        private static readonly object s_cbLock = new object();

        public static CURLcode curlw_easy_setopt_pointer(CURLH handle, CURLoption option, CurlwWriteDataDelegate optval)
        {
            lock (s_cbLock)
            {
                if (!s_handleCallbacks.TryGetValue(handle, out var list))
                {
                    list = new System.Collections.Generic.List<Delegate>();
                    s_handleCallbacks[handle] = list;
                }
                list.Add(optval); // keep alive until the handle is cleaned up/reset
            }
            return curlw_easy_setopt_pointer_cb_imp(handle, option, optval);
        }

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern CURLcode curlw_easy_setopt_string(CURLH handle, CURLoption option, string optval);

        // getinfo: typed variants
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_getinfo_int(CURLH handle, CURLINFO info, out int outval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_getinfo_long(CURLH handle, CURLINFO info, out long outval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_getinfo_double(CURLH handle, CURLINFO info, out double outval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_getinfo_pointer(CURLH handle, CURLINFO info, out IntPtr outval);

        // --- open/close socket callbacks -------------------------------------
        // The native side stores these delegates in process-global function
        // pointers, so the managed delegates MUST outlive every call. The public
        // wrappers below keep static strong references to prevent GC from
        // collecting the thunk (which would crash the next socket callback).
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_easy_set_opensocket_global_cb")]
        private static extern void curlw_easy_set_opensocket_global_cb_imp(CurlwSocketManagedDelegate cb);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_easy_set_closesocket_global_cb")]
        private static extern void curlw_easy_set_closesocket_global_cb_imp(CurlwSocketManagedDelegate cb);

        // Static strong references keeping the global socket delegates alive for
        // the lifetime of the process (matching the native global pointers).
        private static CurlwSocketManagedDelegate s_openSocketCb;
        private static CurlwSocketManagedDelegate s_closeSocketCb;

        public static void curlw_easy_set_opensocket_global_cb(CurlwSocketManagedDelegate cb)
        {
            s_openSocketCb = cb; // keep alive across GC
            curlw_easy_set_opensocket_global_cb_imp(cb);
        }

        public static void curlw_easy_set_closesocket_global_cb(CurlwSocketManagedDelegate cb)
        {
            s_closeSocketCb = cb; // keep alive across GC
            curlw_easy_set_closesocket_global_cb_imp(cb);
        }

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_set_opensocket_cb(CURLH handle, IntPtr userdata);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void curlw_easy_clear_opensocket_cb(CURLH handle);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_easy_set_closesocket_cb(CURLH handle, IntPtr userdata);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void curlw_easy_clear_closesocket_cb(CURLH handle);

        // --- multi API -------------------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMH curlw_multi_init();

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_cleanup(CURLMH multi_handle);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_add_handle(CURLMH multi_handle, CURLH easy_handle);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_remove_handle(CURLMH multi_handle, CURLH easy_handle);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_perform(CURLMH multi_handle, out int running_handles);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_fdset(CURLMH multi_handle, IntPtr read_fds,
                                                         IntPtr write_fds, IntPtr exc_fds, out int max_fd);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_timeout(CURLMH multi_handle, out long milliseconds);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "curlw_multi_strerror_imp")]
        private static extern IntPtr curlw_multi_strerror_imp(CURLMcode error);

        public static string curlw_multi_strerror(CURLMcode error)
        {
            return Marshal.PtrToStringAnsi(curlw_multi_strerror_imp(error));
        }

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_setopt_int(CURLMH multi_handle, int option, int optval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_setopt_long(CURLMH multi_handle, int option, long optval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_setopt_offt(CURLMH multi_handle, int option, long optval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLMcode curlw_multi_setopt_pointer(CURLMH multi_handle, int option, IntPtr optval);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern CURLMcode curlw_multi_setopt_string(CURLMH multi_handle, int option, string optval);

        // Returns a pointer into curl-owned memory. Read its fields with the
        // curlw_msg_* accessors below (no fragile struct-layout marshalling).
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr curlw_multi_info_read(CURLMH multi_handle, out int msgs_in_queue);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int curlw_msg_get_msg(IntPtr msg);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLH curlw_msg_get_easy_handle(IntPtr msg);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern CURLcode curlw_msg_get_result(IntPtr msg);

        // --- slist -----------------------------------------------------------
        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern IntPtr curlw_slist_append(IntPtr list, string value);

        [DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void curlw_slist_free_all(IntPtr list);
    }
}
#endif // !UNITY_WEBGL

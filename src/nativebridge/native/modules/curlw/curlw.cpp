// curlw.cpp — "curlw" module implementation.
//
// A thin, P/Invoke-friendly wrapper over libcurl's easy + multi APIs. Design
// notes:
//   * no external dependencies; the few socket/timing helpers it needs are
//     inlined below (they are used nowhere else).
//   * callback trampolines use curl's official function-pointer types.
//   * setopt long vs off_t split into explicit variants (see curlw.h).
//   * CURLMsg fields exposed via accessor functions (no C# struct-layout ABI).
#include "curlw.h"

#include <cstdint>
#include <chrono>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
using nb_socket_t = SOCKET;
#  ifndef SHUT_RDWR
#    define SHUT_RDWR SD_BOTH
#  endif
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <unistd.h>
#  include <errno.h>
using nb_socket_t = int;
#endif

namespace {

// --- small cross-platform helpers (formerly the yasio bits) -----------------

int nb_socket_last_errno()
{
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

void nb_socket_set_last_errno(int ec)
{
#if defined(_WIN32)
    ::WSASetLastError(ec);
#else
    errno = ec;
#endif
}

int nb_socket_close(nb_socket_t fd)
{
#if defined(_WIN32)
    return ::closesocket(fd);
#else
    return ::close(fd);
#endif
}

int nb_socket_shutdown(nb_socket_t fd)
{
    return ::shutdown(fd, SHUT_RDWR);
}

// Monotonic clock in microseconds — keeps the select() retry loop honest.
int64_t nb_highp_clock_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// A tiny, thread-safe free-list pool of fd_set objects. curl's fdset/select loop
// churns fd_set allocations; pooling avoids per-iteration heap churn.
class fd_set_pool
{
public:
    explicit fd_set_pool(std::size_t chunk = 32) : chunk_(chunk ? chunk : 32) {}
    ~fd_set_pool()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (fd_set* blk : blocks_)
            delete[] blk;
    }

    fd_set* allocate()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (free_.empty())
        {
            fd_set* blk = new fd_set[chunk_];
            blocks_.push_back(blk);
            for (std::size_t i = 0; i < chunk_; ++i)
                free_.push_back(&blk[i]);
        }
        fd_set* p = free_.back();
        free_.pop_back();
        return p;
    }

    void deallocate(fd_set* p)
    {
        if (!p)
            return;
        std::lock_guard<std::mutex> lk(mtx_);
        free_.push_back(p);
    }

private:
    std::mutex           mtx_;
    std::size_t          chunk_;
    std::vector<fd_set*> blocks_; // owned chunk allocations
    std::vector<fd_set*> free_;   // available fd_set slots
};

// Internal fd_set pool, created on global_init, destroyed on global_cleanup.
fd_set_pool* g_fd_set_pool = nullptr;

// Process-global managed callbacks registered from C#.
curlw_socket_managed_cb g_open_cb  = nullptr;
curlw_socket_managed_cb g_close_cb = nullptr;

// curl calls this to open a socket; we forward to the managed open callback.
curl_socket_t open_socket_trampoline(void* clientp, curlsocktype /*purpose*/,
                                     struct curl_sockaddr* address)
{
    // First call with sockfd == -1 asks the managed side whether to allow the
    // socket; a non-zero return means "allow".
    if (g_open_cb && g_open_cb(static_cast<intptr_t>(-1), clientp))
    {
        curl_socket_t fd = static_cast<curl_socket_t>(
            ::socket(address->family, address->socktype, address->protocol));
        // Only notify with the real fd on success. On failure we must NOT call
        // back with -1 again: the managed side can't tell that apart from the
        // initial "asking" sentinel. Just report the failure to curl.
        if (fd != CURL_SOCKET_BAD)
            g_open_cb(static_cast<intptr_t>(fd), clientp);
        return fd;
    }
    return CURL_SOCKET_BAD;
}

// curl calls this to close a socket. If the managed callback claims ownership
// (returns non-zero) we leave the fd alone; otherwise we close it ourselves.
int close_socket_trampoline(void* clientp, curl_socket_t item)
{
    if (!g_close_cb || !g_close_cb(static_cast<intptr_t>(item), clientp))
    {
        if (item != CURL_SOCKET_BAD)
            nb_socket_close(static_cast<nb_socket_t>(item));
    }
    return 0; // CURLE_OK
}
} // namespace

extern "C" {

// --- version / ABI -----------------------------------------------------------
NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_abi_version(void) { return CURLW_ABI_VERSION; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_version_imp(void) { return curl_version(); }

// --- raw socket helpers ------------------------------------------------------
NATIVEBRIDGE_API intptr_t NATIVEBRIDGE_CALL curlw_create_socket(int af, int type, int protocol)
{
    return static_cast<intptr_t>(::socket(af, type, protocol));
}

NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_shutdown_socket(intptr_t sockfd)
{
    return nb_socket_shutdown(static_cast<nb_socket_t>(sockfd));
}

NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_close_socket(intptr_t sockfd)
{
    return nb_socket_close(static_cast<nb_socket_t>(sockfd));
}

NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_errno(void) { return nb_socket_last_errno(); }

// --- global init / cleanup ---------------------------------------------------
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_global_init(int flags, unsigned int max_fd_set)
{
    if (!g_fd_set_pool)
        g_fd_set_pool = new fd_set_pool(max_fd_set);
    return curl_global_init(flags);
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_global_cleanup(void)
{
    delete g_fd_set_pool;
    g_fd_set_pool = nullptr;
    curl_global_cleanup();
}

// --- fd_set pool + select ----------------------------------------------------
NATIVEBRIDGE_API fd_set* NATIVEBRIDGE_CALL curlw_socket_allocfds(void)
{
    return g_fd_set_pool ? g_fd_set_pool->allocate() : nullptr;
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_socket_freefds(fd_set* pfds)
{
    if (g_fd_set_pool)
        g_fd_set_pool->deallocate(pfds);
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_socket_zerofds(fd_set* pfds)
{
    if (pfds)
        FD_ZERO(pfds);
}

NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_socket_select(int nfds, fd_set* readfds, fd_set* writefds,
                                                           fd_set* exceptfds, uint64_t microseconds)
{
    for (;;)
    {
        timeval tv;
        tv.tv_sec  = static_cast<decltype(tv.tv_sec)>(microseconds / 1000000ULL);
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>(microseconds % 1000000ULL);

        int64_t start = nb_highp_clock_us();
        int n = ::select(nfds, readfds, writefds, exceptfds, &tv);

        int64_t elapsed = nb_highp_clock_us() - start;
        microseconds = (elapsed >= static_cast<int64_t>(microseconds)) ? 0 : (microseconds - elapsed);

        if (n < 0 && nb_socket_last_errno() == EINTR)
        {
            if (microseconds > 0)
                continue; // interrupted, time remains -> retry
            n = 0;        // interrupted and out of time -> treat as timeout
        }
        if (n == 0)
            nb_socket_set_last_errno(ETIMEDOUT);
        return n;
    }
}

// --- easy API ----------------------------------------------------------------
NATIVEBRIDGE_API CURL* NATIVEBRIDGE_CALL curlw_easy_init(void) { return curl_easy_init(); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_perform(CURL* handle) { return curl_easy_perform(handle); }
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_cleanup(CURL* handle) { curl_easy_cleanup(handle); }
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_reset(CURL* handle) { curl_easy_reset(handle); }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_easy_strerror_imp(CURLcode error) { return curl_easy_strerror(error); }

NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_int(CURL* handle, CURLoption option, int optval)
{
    return curl_easy_setopt(handle, option, static_cast<long>(optval));
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_long(CURL* handle, CURLoption option, int64_t optval)
{
    // int64_t across the ABI (matches C# `long`); narrow to curl's native `long`.
    return curl_easy_setopt(handle, option, static_cast<long>(optval));
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_offt(CURL* handle, CURLoption option, int64_t optval)
{
    return curl_easy_setopt(handle, option, static_cast<curl_off_t>(optval));
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_pointer(CURL* handle, CURLoption option, void* optval)
{
    return curl_easy_setopt(handle, option, optval);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_string(CURL* handle, CURLoption option, const char* optval)
{
    return curl_easy_setopt(handle, option, optval);
}

NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_int(CURL* handle, CURLINFO info, int* outval)
{
    long tmp = 0;
    CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
    if (outval)
        *outval = static_cast<int>(tmp);
    return ec;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_long(CURL* handle, CURLINFO info, int64_t* outval)
{
    // CURLINFO_*_T fields are curl_off_t; plain LONG fields are long. Reading a
    // long field: read as long then widen. To keep one entry point we branch on
    // the info type mask.
    if ((info & CURLINFO_TYPEMASK) == CURLINFO_OFF_T)
    {
        curl_off_t tmp = 0;
        CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
        if (outval)
            *outval = static_cast<int64_t>(tmp);
        return ec;
    }
    long tmp = 0;
    CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
    if (outval)
        *outval = static_cast<int64_t>(tmp);
    return ec;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_double(CURL* handle, CURLINFO info, double* outval)
{
    return curl_easy_getinfo(handle, info, outval);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_pointer(CURL* handle, CURLINFO info, void** outval)
{
    return curl_easy_getinfo(handle, info, outval);
}

// --- open/close socket callbacks --------------------------------------------
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_set_opensocket_global_cb(curlw_socket_managed_cb cb)
{
    g_open_cb = cb;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_set_opensocket_cb(CURL* handle, void* userdata)
{
    if (!g_open_cb)
        return CURLE_FAILED_INIT; // register the global callback first
    CURLcode res = curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, userdata);
    if (res == CURLE_OK)
        res = curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, open_socket_trampoline);
    return res;
}
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_clear_opensocket_cb(CURL* handle)
{
    curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, (curl_opensocket_callback)nullptr);
    curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, (void*)nullptr);
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_set_closesocket_global_cb(curlw_socket_managed_cb cb)
{
    g_close_cb = cb;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_set_closesocket_cb(CURL* handle, void* userdata)
{
    if (!g_close_cb)
        return CURLE_FAILED_INIT; // register the global callback first
    CURLcode res = curl_easy_setopt(handle, CURLOPT_CLOSESOCKETDATA, userdata);
    if (res == CURLE_OK)
        // FIX: reference bug wired the *open* trampoline here.
        res = curl_easy_setopt(handle, CURLOPT_CLOSESOCKETFUNCTION, close_socket_trampoline);
    return res;
}
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_clear_closesocket_cb(CURL* handle)
{
    curl_easy_setopt(handle, CURLOPT_CLOSESOCKETFUNCTION, (curl_closesocket_callback)nullptr);
    curl_easy_setopt(handle, CURLOPT_CLOSESOCKETDATA, (void*)nullptr);
}

// --- multi API ---------------------------------------------------------------
NATIVEBRIDGE_API CURLM* NATIVEBRIDGE_CALL curlw_multi_init(void) { return curl_multi_init(); }
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_cleanup(CURLM* multi_handle) { return curl_multi_cleanup(multi_handle); }
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_add_handle(CURLM* multi_handle, CURL* easy_handle)
{
    return curl_multi_add_handle(multi_handle, easy_handle);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_remove_handle(CURLM* multi_handle, CURL* easy_handle)
{
    return curl_multi_remove_handle(multi_handle, easy_handle);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_perform(CURLM* multi_handle, int* running_handles)
{
    return curl_multi_perform(multi_handle, running_handles);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_fdset(CURLM* multi_handle, fd_set* read_fds,
                                                              fd_set* write_fds, fd_set* exc_fds, int* max_fd)
{
    return curl_multi_fdset(multi_handle, read_fds, write_fds, exc_fds, max_fd);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_timeout(CURLM* multi_handle, int64_t* milliseconds)
{
    long ms = 0;
    CURLMcode ec = curl_multi_timeout(multi_handle, &ms);
    if (milliseconds)
        *milliseconds = static_cast<int64_t>(ms);
    return ec;
}
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_multi_strerror_imp(CURLMcode error)
{
    return curl_multi_strerror(error);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_int(CURLM* multi_handle, CURLMoption option, int optval)
{
    return curl_multi_setopt(multi_handle, option, static_cast<long>(optval));
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_long(CURLM* multi_handle, CURLMoption option, int64_t optval)
{
    // int64_t across the ABI (matches C# `long`); narrow to curl's native `long`.
    return curl_multi_setopt(multi_handle, option, static_cast<long>(optval));
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_offt(CURLM* multi_handle, CURLMoption option, int64_t optval)
{
    return curl_multi_setopt(multi_handle, option, static_cast<curl_off_t>(optval));
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_pointer(CURLM* multi_handle, CURLMoption option, void* optval)
{
    return curl_multi_setopt(multi_handle, option, optval);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_string(CURLM* multi_handle, CURLMoption option, const char* optval)
{
    return curl_multi_setopt(multi_handle, option, optval);
}

NATIVEBRIDGE_API CURLMsg* NATIVEBRIDGE_CALL curlw_multi_info_read(CURLM* multi_handle, int* msgs_in_queue)
{
    return curl_multi_info_read(multi_handle, msgs_in_queue);
}
NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_msg_get_msg(const CURLMsg* msg)
{
    return msg ? static_cast<int>(msg->msg) : 0;
}
NATIVEBRIDGE_API CURL* NATIVEBRIDGE_CALL curlw_msg_get_easy_handle(const CURLMsg* msg)
{
    return msg ? msg->easy_handle : nullptr;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_msg_get_result(const CURLMsg* msg)
{
    return msg ? msg->data.result : CURLE_OK;
}

// --- slist -------------------------------------------------------------------
NATIVEBRIDGE_API struct curl_slist* NATIVEBRIDGE_CALL curlw_slist_append(struct curl_slist* list, const char* value)
{
    return curl_slist_append(list, value);
}
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_slist_free_all(struct curl_slist* list)
{
    curl_slist_free_all(list);
}

} // extern "C"

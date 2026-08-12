// curlw.cpp — "curlw" module implementation.
//
// A thin, P/Invoke-friendly wrapper over libcurl's easy + multi APIs. Design
// notes:
//   * no external dependencies; the few socket/timing helpers it needs are
//     inlined below (they are used nowhere else).
//   * callback trampolines use curl's official function-pointer types.
//   * setopt long vs off_t split into explicit variants (see curlw.h).
//   * CURLMsg fields exposed via accessor functions (no C# struct-layout ABI).

// Prevent <windows.h> (pulled in by winsock2.h / curl.h on Win32) from defining
// min/max as function-like macros, which would break std::numeric_limits::min/max
// with C4003/C2589. Must come before any header that includes windows.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "curlw.h"

#include <atomic>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
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

#if defined(_WIN32)
constexpr int nb_socket_interrupted = WSAEINTR;
constexpr int nb_socket_timed_out   = WSAETIMEDOUT;
#else
constexpr int nb_socket_interrupted = EINTR;
constexpr int nb_socket_timed_out   = ETIMEDOUT;
#endif

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

template <typename T>
int curlw_option_value(T option)
{
    return static_cast<int>(option);
}

template <typename T>
bool curlw_is_long_option(T option)
{
    const int value = curlw_option_value(option);
    return value > CURLOPTTYPE_LONG && value < CURLOPTTYPE_OBJECTPOINT;
}

template <typename T>
bool curlw_is_object_option(T option)
{
    const int value = curlw_option_value(option);
    return value > CURLOPTTYPE_OBJECTPOINT && value < CURLOPTTYPE_FUNCTIONPOINT;
}

template <typename T>
bool curlw_is_pointer_option(T option)
{
    const int value = curlw_option_value(option);
    return value > CURLOPTTYPE_OBJECTPOINT && value < CURLOPTTYPE_OFF_T;
}

template <typename T>
bool curlw_is_offt_option(T option)
{
    const int value = curlw_option_value(option);
    return value > CURLOPTTYPE_OFF_T && value < CURLOPTTYPE_BLOB;
}

bool curlw_is_blob_option(CURLoption option)
{
    return curlw_option_value(option) > CURLOPTTYPE_BLOB;
}

int curlw_info_type(CURLINFO info)
{
    return static_cast<int>(info) & CURLINFO_TYPEMASK;
}

bool curlw_allows_unsigned_long(CURLoption option)
{
    switch (option)
    {
    case CURLOPT_HTTPAUTH:
    case CURLOPT_PROXYAUTH:
    case CURLOPT_SOCKS5_AUTH:
    case CURLOPT_SSH_AUTH_TYPES:
    case CURLOPT_PROTOCOLS:
    case CURLOPT_REDIR_PROTOCOLS:
        return true;
    default:
        return false;
    }
}

bool curlw_allows_unsigned_long(CURLMoption option)
{
    return option == CURLMOPT_MAXCONNECTS;
}

// C# has no native-width long. On 32-bit-long ABIs preserve the bit pattern for
// options whose public contract is an unsigned 32-bit mask; reject other values
// outside the signed native-long range.
template <typename T>
bool curlw_try_native_long(T option, int64_t value, long& result)
{
    static_assert(sizeof(long) == 4 || sizeof(long) == 8, "unsupported native long width");

    if constexpr (sizeof(long) == sizeof(int64_t))
    {
        result = static_cast<long>(value);
        return true;
    }

    if (value < static_cast<int64_t>(std::numeric_limits<long>::min()))
        return false;
    if (value > static_cast<int64_t>(std::numeric_limits<long>::max()) &&
        (!curlw_allows_unsigned_long(option) ||
         static_cast<uint64_t>(value) > static_cast<uint64_t>(std::numeric_limits<unsigned long>::max())))
        return false;

    const unsigned long bits = static_cast<unsigned long>(value);
    std::memcpy(&result, &bits, sizeof(result));
    return true;
}

// A tiny, thread-safe free-list pool of fd_set objects. curl's fdset/select loop
// churns fd_set allocations; pooling avoids per-iteration heap churn.
class fd_set_pool
{
public:
    explicit fd_set_pool(std::size_t chunk = 32) : chunk_(chunk ? chunk : 32) {}

    fd_set_pool(const fd_set_pool&) = delete;
    fd_set_pool& operator=(const fd_set_pool&) = delete;

    fd_set* allocate()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (free_.empty())
        {
            free_.reserve(free_.size() + chunk_);
            auto block = std::make_unique<fd_set[]>(chunk_);
            fd_set* blk = block.get();
            blocks_.push_back(std::move(block));
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
    std::mutex                             mtx_;
    std::size_t                            chunk_;
    std::vector<std::unique_ptr<fd_set[]>> blocks_; // owned chunk allocations
    std::vector<fd_set*>                   free_;   // available fd_set slots
};

// Internal fd_set pool follows libcurl's global-init reference count.
// The pool pointer is atomic so the alloc/free hot path stays lock-free once
// init has finished; g_global_mutex only serializes init/cleanup transitions.
std::mutex               g_global_mutex;
unsigned int             g_global_init_count = 0;
std::atomic<fd_set_pool*> g_fd_set_pool{nullptr};

// Process-global managed callbacks registered from C#.
std::atomic<curlw_socket_managed_cb> g_open_cb{nullptr};
std::atomic<curlw_socket_managed_cb> g_close_cb{nullptr};

struct share_lock_set
{
    std::mutex locks[CURL_LOCK_DATA_LAST];
};

std::mutex g_share_locks_mutex;
std::unordered_map<CURLSH*, std::unique_ptr<share_lock_set>> g_share_locks;

void share_lock(CURL*, curl_lock_data data, curl_lock_access, void* userptr)
{
    auto* lock_set = static_cast<share_lock_set*>(userptr);
    if (lock_set && static_cast<unsigned>(data) < CURL_LOCK_DATA_LAST)
        lock_set->locks[data].lock();
}

void share_unlock(CURL*, curl_lock_data data, void* userptr)
{
    auto* lock_set = static_cast<share_lock_set*>(userptr);
    if (lock_set && static_cast<unsigned>(data) < CURL_LOCK_DATA_LAST)
        lock_set->locks[data].unlock();
}

// curl calls this to open a socket; we forward to the managed open callback.
curl_socket_t open_socket_trampoline(void* clientp, curlsocktype /*purpose*/,
                                     struct curl_sockaddr* address)
{
    // First call with sockfd == -1 asks the managed side whether to allow the
    // socket; a non-zero return means "allow".
    curlw_socket_managed_cb cb = g_open_cb.load(std::memory_order_acquire);
    if (cb && cb(static_cast<intptr_t>(-1), clientp))
    {
        curl_socket_t fd = static_cast<curl_socket_t>(
            ::socket(address->family, address->socktype, address->protocol));
        // Only notify with the real fd on success. On failure we must NOT call
        // back with -1 again: the managed side can't tell that apart from the
        // initial "asking" sentinel. Just report the failure to curl.
        if (fd != CURL_SOCKET_BAD)
            cb(static_cast<intptr_t>(fd), clientp);
        return fd;
    }
    return CURL_SOCKET_BAD;
}

// curl calls this to close a socket. If the managed callback claims ownership
// (returns non-zero) we leave the fd alone; otherwise we close it ourselves.
int close_socket_trampoline(void* clientp, curl_socket_t item)
{
    curlw_socket_managed_cb cb = g_close_cb.load(std::memory_order_acquire);
    if (cb && cb(static_cast<intptr_t>(item), clientp))
        return 0;
    if (item == CURL_SOCKET_BAD)
        return 0;
    return nb_socket_close(static_cast<nb_socket_t>(item)) == 0 ? 0 : 1;
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
    std::lock_guard<std::mutex> lk(g_global_mutex);

    CURLcode ec = curl_global_init(static_cast<long>(flags));
    if (ec != CURLE_OK)
        return ec;

    if (g_global_init_count == 0)
    {
        auto* pool = new (std::nothrow) fd_set_pool(max_fd_set);
        if (!pool)
        {
            curl_global_cleanup();
            return CURLE_OUT_OF_MEMORY;
        }
        g_fd_set_pool.store(pool, std::memory_order_release);
    }
    ++g_global_init_count;
    return CURLE_OK;
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_global_cleanup(void)
{
    std::lock_guard<std::mutex> lk(g_global_mutex);
    if (g_global_init_count == 0)
        return;

    curl_global_cleanup();
    if (--g_global_init_count == 0)
    {
        delete g_fd_set_pool.exchange(nullptr, std::memory_order_acq_rel);
    }
}

// --- fd_set pool + select ----------------------------------------------------
NATIVEBRIDGE_API fd_set* NATIVEBRIDGE_CALL curlw_socket_allocfds(void)
{
    fd_set_pool* pool = g_fd_set_pool.load(std::memory_order_acquire);
    return pool ? pool->allocate() : nullptr;
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_socket_freefds(fd_set* pfds)
{
    fd_set_pool* pool = g_fd_set_pool.load(std::memory_order_acquire);
    if (pool)
        pool->deallocate(pfds);
}

NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_socket_zerofds(fd_set* pfds)
{
    if (pfds)
        FD_ZERO(pfds);
}

NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_socket_select(int nfds, fd_set* readfds, fd_set* writefds,
                                                           fd_set* exceptfds, uint64_t microseconds)
{
    // Snapshot the input fd_sets so an EINTR retry re-arms with the caller's set
    // rather than the (implementation-defined) modified state left by select.
    fd_set readfds_input;
    fd_set writefds_input;
    fd_set exceptfds_input;
    if (readfds)
        readfds_input = *readfds;
    if (writefds)
        writefds_input = *writefds;
    if (exceptfds)
        exceptfds_input = *exceptfds;

    for (;;)
    {
        timeval tv;
        tv.tv_sec  = static_cast<decltype(tv.tv_sec)>(microseconds / 1000000ULL);
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>(microseconds % 1000000ULL);

        const int64_t start = nb_highp_clock_us();
        int n = ::select(nfds, readfds, writefds, exceptfds, &tv);

        if (n < 0 && nb_socket_last_errno() == nb_socket_interrupted)
        {
            const int64_t elapsed = nb_highp_clock_us() - start;
            const uint64_t elapsed_us = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;
            if (elapsed_us < microseconds)
            {
                microseconds -= elapsed_us;
                if (readfds)   *readfds   = readfds_input;
                if (writefds)  *writefds  = writefds_input;
                if (exceptfds) *exceptfds = exceptfds_input;
                continue;
            }
            n = 0; // interrupted with no time left → surface as timeout
        }
        if (n == 0)
            nb_socket_set_last_errno(nb_socket_timed_out);
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
    if (!curlw_is_long_option(option))
        return CURLE_BAD_FUNCTION_ARGUMENT;
    return curl_easy_setopt(handle, option, static_cast<long>(optval));
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_long(CURL* handle, CURLoption option, int64_t optval)
{
    long native_value = 0;
    if (!curlw_is_long_option(option) || !curlw_try_native_long(option, optval, native_value))
        return CURLE_BAD_FUNCTION_ARGUMENT;
    return curl_easy_setopt(handle, option, native_value);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_offt(CURL* handle, CURLoption option, int64_t optval)
{
    if (!curlw_is_offt_option(option))
        return CURLE_BAD_FUNCTION_ARGUMENT;
    return curl_easy_setopt(handle, option, static_cast<curl_off_t>(optval));
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_pointer(CURL* handle, CURLoption option, void* optval)
{
    if (!curlw_is_pointer_option(option))
        return CURLE_BAD_FUNCTION_ARGUMENT;
    return curl_easy_setopt(handle, option, optval);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_string(CURL* handle, CURLoption option, const char* optval)
{
    if (!curlw_is_object_option(option))
        return CURLE_BAD_FUNCTION_ARGUMENT;
    return curl_easy_setopt(handle, option, optval);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_setopt_blob(CURL* handle, CURLoption option,
                                                                  void* data, size_t len, unsigned int flags)
{
    if (!curlw_is_blob_option(option))
        return CURLE_BAD_FUNCTION_ARGUMENT;
    struct curl_blob blob;
    blob.data  = data;
    blob.len   = len;
    blob.flags = flags; // CURL_BLOB_COPY (1) => curl owns a copy
    return curl_easy_setopt(handle, option, &blob);
}

NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_int(CURL* handle, CURLINFO info, int* outval)
{
    if (!outval || curlw_info_type(info) != CURLINFO_LONG)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    long tmp = 0;
    CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
    if (ec == CURLE_OK)
    {
        if (tmp < static_cast<long>(std::numeric_limits<int>::min()) ||
            tmp > static_cast<long>(std::numeric_limits<int>::max()))
            return CURLE_BAD_FUNCTION_ARGUMENT;
        *outval = static_cast<int>(tmp);
    }
    return ec;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_long(CURL* handle, CURLINFO info, int64_t* outval)
{
    if (!outval)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    const int type = curlw_info_type(info);
    if (type == CURLINFO_OFF_T)
    {
        curl_off_t tmp = 0;
        CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
        if (ec == CURLE_OK)
            *outval = static_cast<int64_t>(tmp);
        return ec;
    }
    if (type != CURLINFO_LONG)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    long tmp = 0;
    CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
    if (ec == CURLE_OK)
        *outval = static_cast<int64_t>(tmp);
    return ec;
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_double(CURL* handle, CURLINFO info, double* outval)
{
    if (!outval || curlw_info_type(info) != CURLINFO_DOUBLE)
        return CURLE_BAD_FUNCTION_ARGUMENT;
    return curl_easy_getinfo(handle, info, outval);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_getinfo_pointer(CURL* handle, CURLINFO info, void** outval)
{
    if (!outval)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    const int type = curlw_info_type(info);
    if (type == CURLINFO_STRING)
    {
        const char* tmp = nullptr;
        CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
        if (ec == CURLE_OK)
            *outval = const_cast<char*>(tmp);
        return ec;
    }
    if (type == CURLINFO_SLIST)
    {
        struct curl_slist* tmp = nullptr;
        CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
        if (ec == CURLE_OK)
            *outval = static_cast<void*>(tmp);
        return ec;
    }
    if (type == CURLINFO_SOCKET)
    {
        curl_socket_t tmp = CURL_SOCKET_BAD;
        CURLcode ec = curl_easy_getinfo(handle, info, &tmp);
        if (ec == CURLE_OK)
            *outval = reinterpret_cast<void*>(static_cast<uintptr_t>(tmp));
        return ec;
    }
    return CURLE_BAD_FUNCTION_ARGUMENT;
}

// --- open/close socket callbacks --------------------------------------------
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_easy_set_opensocket_global_cb(curlw_socket_managed_cb cb)
{
    g_open_cb.store(cb, std::memory_order_release);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_set_opensocket_cb(CURL* handle, void* userdata)
{
    if (!g_open_cb.load(std::memory_order_acquire))
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
    g_close_cb.store(cb, std::memory_order_release);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_set_closesocket_cb(CURL* handle, void* userdata)
{
    if (!g_close_cb.load(std::memory_order_acquire))
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
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_poll(CURLM* multi_handle, struct curl_waitfd* extra_fds,
                                                             unsigned int extra_nfds, int timeout_ms, int* numfds)
{
    return curl_multi_poll(multi_handle, extra_fds, extra_nfds, timeout_ms, numfds);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_wait(CURLM* multi_handle, struct curl_waitfd* extra_fds,
                                                             unsigned int extra_nfds, int timeout_ms, int* numfds)
{
    return curl_multi_wait(multi_handle, extra_fds, extra_nfds, timeout_ms, numfds);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_wakeup(CURLM* multi_handle)
{
    return curl_multi_wakeup(multi_handle);
}
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_multi_strerror_imp(CURLMcode error)
{
    return curl_multi_strerror(error);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_int(CURLM* multi_handle, CURLMoption option, int optval)
{
    if (!curlw_is_long_option(option))
        return CURLM_BAD_FUNCTION_ARGUMENT;
    return curl_multi_setopt(multi_handle, option, static_cast<long>(optval));
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_long(CURLM* multi_handle, CURLMoption option, int64_t optval)
{
    long native_value = 0;
    if (!curlw_is_long_option(option) || !curlw_try_native_long(option, optval, native_value))
        return CURLM_BAD_FUNCTION_ARGUMENT;
    return curl_multi_setopt(multi_handle, option, native_value);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_offt(CURLM* multi_handle, CURLMoption option, int64_t optval)
{
    if (!curlw_is_offt_option(option))
        return CURLM_BAD_FUNCTION_ARGUMENT;
    return curl_multi_setopt(multi_handle, option, static_cast<curl_off_t>(optval));
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_pointer(CURLM* multi_handle, CURLMoption option, void* optval)
{
    if (!curlw_is_pointer_option(option))
        return CURLM_BAD_FUNCTION_ARGUMENT;
    return curl_multi_setopt(multi_handle, option, optval);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_setopt_string(CURLM* multi_handle, CURLMoption option, const char* optval)
{
    if (!curlw_is_object_option(option))
        return CURLM_BAD_FUNCTION_ARGUMENT;
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

// --- misc / memory -----------------------------------------------------------
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_free(void* p) { curl_free(p); }

// --- easy: extra entry points ------------------------------------------------
NATIVEBRIDGE_API CURL* NATIVEBRIDGE_CALL curlw_easy_duphandle(CURL* handle) { return curl_easy_duphandle(handle); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_pause(CURL* handle, int action) { return curl_easy_pause(handle, action); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_upkeep(CURL* handle) { return curl_easy_upkeep(handle); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_recv(CURL* handle, void* buffer, size_t buflen, size_t* n)
{
    return curl_easy_recv(handle, buffer, buflen, n);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_easy_send(CURL* handle, const void* buffer, size_t buflen, size_t* n)
{
    return curl_easy_send(handle, buffer, buflen, n);
}
NATIVEBRIDGE_API char* NATIVEBRIDGE_CALL curlw_easy_escape(CURL* handle, const char* string, int length)
{
    return curl_easy_escape(handle, string, length);
}
NATIVEBRIDGE_API char* NATIVEBRIDGE_CALL curlw_easy_unescape(CURL* handle, const char* string, int inlength, int* outlength)
{
    return curl_easy_unescape(handle, string, inlength, outlength);
}

// --- version info ------------------------------------------------------------
NATIVEBRIDGE_API const curl_version_info_data* NATIVEBRIDGE_CALL curlw_version_info(void)
{
    return curl_version_info(CURLVERSION_NOW);
}
NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_verinfo_features(const curl_version_info_data* d) { return d ? d->features : 0; }
NATIVEBRIDGE_API unsigned int NATIVEBRIDGE_CALL curlw_verinfo_version_num(const curl_version_info_data* d) { return d ? d->version_num : 0; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_version(const curl_version_info_data* d) { return d ? d->version : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_ssl_version(const curl_version_info_data* d) { return d ? d->ssl_version : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_libz_version(const curl_version_info_data* d) { return d ? d->libz_version : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_nghttp2_version(const curl_version_info_data* d) { return d ? d->nghttp2_version : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_quic_version(const curl_version_info_data* d) { return d ? d->quic_version : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_cainfo(const curl_version_info_data* d) { return d ? d->cainfo : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_verinfo_capath(const curl_version_info_data* d) { return d ? d->capath : nullptr; }

// --- header API --------------------------------------------------------------
NATIVEBRIDGE_API CURLHcode NATIVEBRIDGE_CALL curlw_easy_header(CURL* handle, const char* name, size_t nameindex,
                                                              unsigned int origin, int request, struct curl_header** hout)
{
    return curl_easy_header(handle, name, nameindex, origin, request, hout);
}
NATIVEBRIDGE_API struct curl_header* NATIVEBRIDGE_CALL curlw_easy_nextheader(CURL* handle, unsigned int origin,
                                                                            int request, struct curl_header* prev)
{
    return curl_easy_nextheader(handle, origin, request, prev);
}
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_header_name(const struct curl_header* h) { return h ? h->name : nullptr; }
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_header_value(const struct curl_header* h) { return h ? h->value : nullptr; }
NATIVEBRIDGE_API size_t NATIVEBRIDGE_CALL curlw_header_amount(const struct curl_header* h) { return h ? h->amount : 0; }
NATIVEBRIDGE_API size_t NATIVEBRIDGE_CALL curlw_header_index(const struct curl_header* h) { return h ? h->index : 0; }
NATIVEBRIDGE_API unsigned int NATIVEBRIDGE_CALL curlw_header_origin(const struct curl_header* h) { return h ? h->origin : 0; }

// --- share API ---------------------------------------------------------------
NATIVEBRIDGE_API CURLSH* NATIVEBRIDGE_CALL curlw_share_init(void) { return curl_share_init(); }
NATIVEBRIDGE_API CURLSHcode NATIVEBRIDGE_CALL curlw_share_cleanup(CURLSH* share)
{
    std::lock_guard<std::mutex> lk(g_share_locks_mutex);
    CURLSHcode ec = curl_share_cleanup(share);
    if (ec == CURLSHE_OK)
        g_share_locks.erase(share);
    return ec;
}
NATIVEBRIDGE_API CURLSHcode NATIVEBRIDGE_CALL curlw_share_setopt_int(CURLSH* share, CURLSHoption option, int value)
{
    if (option != CURLSHOPT_SHARE && option != CURLSHOPT_UNSHARE)
        return CURLSHE_BAD_OPTION;
    // curl_share_setopt reads SHARE/UNSHARE via va_arg(param, int) — pass int
    // directly. Widening to long here is UB on LP64 (reads 8 bytes for a 4-byte
    // slot). Contrast with curl_easy_setopt/curl_multi_setopt LONG options,
    // which do use va_arg(param, long).
    return curl_share_setopt(share, option, value);
}
NATIVEBRIDGE_API CURLSHcode NATIVEBRIDGE_CALL curlw_share_enable_default_locks(CURLSH* share)
{
    if (!share)
        return CURLSHE_INVALID;

    std::lock_guard<std::mutex> lk(g_share_locks_mutex);
    auto [it, inserted] = g_share_locks.try_emplace(share);
    if (!inserted)
        return CURLSHE_OK; // already installed for this share

    it->second = std::unique_ptr<share_lock_set>(new (std::nothrow) share_lock_set);
    if (!it->second)
    {
        g_share_locks.erase(it);
        return CURLSHE_NOMEM;
    }

    // Roll back the map entry and detach callbacks if any setopt call fails.
    // The share is not yet in use, so clearing callbacks is safe.
    bool committed = false;
    struct Rollback
    {
        CURLSH* share;
        std::unordered_map<CURLSH*, std::unique_ptr<share_lock_set>>::iterator it;
        bool& committed;
        ~Rollback()
        {
            if (committed) return;
            curl_share_setopt(share, CURLSHOPT_LOCKFUNC, static_cast<curl_lock_function>(nullptr));
            curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, static_cast<curl_unlock_function>(nullptr));
            curl_share_setopt(share, CURLSHOPT_USERDATA, static_cast<void*>(nullptr));
            g_share_locks.erase(it);
        }
    } guard{share, it, committed};

    CURLSHcode ec = curl_share_setopt(share, CURLSHOPT_USERDATA, it->second.get());
    if (ec == CURLSHE_OK)
        ec = curl_share_setopt(share, CURLSHOPT_LOCKFUNC, &share_lock);
    if (ec == CURLSHE_OK)
        ec = curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, &share_unlock);

    committed = (ec == CURLSHE_OK);
    return ec;
}
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_share_strerror_imp(CURLSHcode error) { return curl_share_strerror(error); }

// --- MIME --------------------------------------------------------------------
NATIVEBRIDGE_API curl_mime* NATIVEBRIDGE_CALL curlw_mime_init(CURL* easy) { return curl_mime_init(easy); }
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_mime_free(curl_mime* mime) { curl_mime_free(mime); }
NATIVEBRIDGE_API curl_mimepart* NATIVEBRIDGE_CALL curlw_mime_addpart(curl_mime* mime) { return curl_mime_addpart(mime); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_name(curl_mimepart* part, const char* name) { return curl_mime_name(part, name); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_data(curl_mimepart* part, const char* data, size_t datasize) { return curl_mime_data(part, data, datasize); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_filedata(curl_mimepart* part, const char* filename) { return curl_mime_filedata(part, filename); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_filename(curl_mimepart* part, const char* filename) { return curl_mime_filename(part, filename); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_type(curl_mimepart* part, const char* mimetype) { return curl_mime_type(part, mimetype); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_encoder(curl_mimepart* part, const char* encoding) { return curl_mime_encoder(part, encoding); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_headers(curl_mimepart* part, struct curl_slist* headers, int take_ownership) { return curl_mime_headers(part, headers, take_ownership); }
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_mime_subparts(curl_mimepart* part, curl_mime* subparts) { return curl_mime_subparts(part, subparts); }

// --- URL API -----------------------------------------------------------------
NATIVEBRIDGE_API CURLU* NATIVEBRIDGE_CALL curlw_url(void) { return curl_url(); }
NATIVEBRIDGE_API void NATIVEBRIDGE_CALL curlw_url_cleanup(CURLU* handle) { curl_url_cleanup(handle); }
NATIVEBRIDGE_API CURLU* NATIVEBRIDGE_CALL curlw_url_dup(const CURLU* in) { return curl_url_dup(in); }
NATIVEBRIDGE_API CURLUcode NATIVEBRIDGE_CALL curlw_url_get(const CURLU* handle, CURLUPart what, char** part, unsigned int flags)
{
    return curl_url_get(handle, what, part, flags);
}
NATIVEBRIDGE_API CURLUcode NATIVEBRIDGE_CALL curlw_url_set(CURLU* handle, CURLUPart what, const char* part, unsigned int flags)
{
    return curl_url_set(handle, what, part, flags);
}
NATIVEBRIDGE_API const char* NATIVEBRIDGE_CALL curlw_url_strerror_imp(CURLUcode error) { return curl_url_strerror(error); }

// --- WebSocket ---------------------------------------------------------------
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_ws_recv(CURL* handle, void* buffer, size_t buflen,
                                                         size_t* recv, const struct curl_ws_frame** meta)
{
    return curl_ws_recv(handle, buffer, buflen, recv, meta);
}
NATIVEBRIDGE_API CURLcode NATIVEBRIDGE_CALL curlw_ws_send(CURL* handle, const void* buffer, size_t buflen,
                                                         size_t* sent, int64_t fragsize, unsigned int flags)
{
    return curl_ws_send(handle, buffer, buflen, sent, static_cast<curl_off_t>(fragsize), flags);
}
NATIVEBRIDGE_API const struct curl_ws_frame* NATIVEBRIDGE_CALL curlw_ws_meta(CURL* handle) { return curl_ws_meta(handle); }
NATIVEBRIDGE_API int NATIVEBRIDGE_CALL curlw_wsframe_flags(const struct curl_ws_frame* f) { return f ? f->flags : 0; }
NATIVEBRIDGE_API int64_t NATIVEBRIDGE_CALL curlw_wsframe_offset(const struct curl_ws_frame* f) { return f ? static_cast<int64_t>(f->offset) : 0; }
NATIVEBRIDGE_API int64_t NATIVEBRIDGE_CALL curlw_wsframe_bytesleft(const struct curl_ws_frame* f) { return f ? static_cast<int64_t>(f->bytesleft) : 0; }
NATIVEBRIDGE_API size_t NATIVEBRIDGE_CALL curlw_wsframe_len(const struct curl_ws_frame* f) { return f ? f->len : 0; }

// --- multi: event-driven extensions ------------------------------------------
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_assign(CURLM* multi_handle, intptr_t sockfd, void* sockp)
{
    return curl_multi_assign(multi_handle, static_cast<curl_socket_t>(sockfd), sockp);
}
NATIVEBRIDGE_API CURLMcode NATIVEBRIDGE_CALL curlw_multi_socket_action(CURLM* multi_handle, intptr_t s,
                                                                      int ev_bitmask, int* running_handles)
{
    return curl_multi_socket_action(multi_handle, static_cast<curl_socket_t>(s), ev_bitmask, running_handles);
}
NATIVEBRIDGE_API CURL** NATIVEBRIDGE_CALL curlw_multi_get_handles(CURLM* multi_handle)
{
    return curl_multi_get_handles(multi_handle);
}

} // extern "C"

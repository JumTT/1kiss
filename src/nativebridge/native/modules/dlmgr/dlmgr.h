// dlmgr.h — authoritative C ABI for the "dlmgr" module (download manager).
//
// This header is the SINGLE SOURCE OF TRUTH for the native<->C# contract.
// DownloadManager.cs mirrors these declarations one-for-one. Every function:
//   * is extern "C" (no name mangling),
//   * is decorated with NATIVEBRIDGE_API (exported),
//   * uses NATIVEBRIDGE_CALL == cdecl (matches CallingConvention.Cdecl in C#).
//
// Model (design doc §3): a self-contained download manager with N worker
// threads (one blocking curl easy handle each, single connection per task),
// a global curl share (DNS cache + default locks), DOH injection, dual-URL
// fallback (range_url probed with "Range: bytes=0-" via three gates, one
// full_url re-download from zero per task, not consuming retries), in-session
// Range resume, per-task + global token-bucket rate limiting (effective rate
// = min; 0 = unlimited), and a 10 Hz reporter thread driving the callbacks
// (per-task first, then global).
//
// Sinks: the manager never touches VFS. A sink is registered as a u64 handle
// (Rust sink registry with a C ABI vtable {ctx, prepare, write, reset,
// finish, abort}); glue (vfs<->dlmgr bridge) provides VFS-backed sinks,
// dlmgr_sink_create_file provides plain-file ones. The host MUST call
// sink release only after the task reached a terminal state (Done/Failed/
// Canceled was reported). Sinks are identified by u64 handle; 0 is invalid.
//
// Handles: dlmgr_create returns an opaque handle consumed by exactly one
// dlmgr_shutdown (which cancels all tasks, joins every worker/reporter thread
// and frees the manager — it cannot deadlock: cancel aborts in-flight
// transfers through the write callback, and stalled transfers are cut by
// connection/stall timeouts). After shutdown the handle memory is FREED:
// calling any other function with it is use-after-free, not a graceful
// failure — never touch the handle again. A NULL handle is rejected with
// failure return values.
//
// Callback keep-alive: the native side stores RAW function pointers. C#
// delegates marshalled as these pointers MUST stay strongly referenced for as
// long as they are registered (see DownloadManager.cs — it pins them for you)
// and MUST be static + [MonoPInvokeCallback] on IL2CPP/AOT. Callbacks fire on
// the reporter thread at ~10 Hz; heavy dispatch to the main thread is the C#
// side's job.
//
// Strings cross the ABI as NUL-terminated UTF-8 (const char*). No structs are
// passed across the ABI. panic=abort: FFI boundaries do not catch unwinds.
//
// ABI versioning: bump DLMGR_ABI_VERSION whenever an existing signature
// changes (a C# breaking change). Purely additive exports do not bump it.
#ifndef DLMGR_H
#define DLMGR_H

#include "nativebridge.h"
#include <stdint.h>

#define DLMGR_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// --- task states (task callback `state`) --------------------------------------
enum DlmgrTaskState {
    DLMGR_TASK_PENDING   = 0,  // queued, not yet picked by a worker
    DLMGR_TASK_RUNNING   = 1,
    DLMGR_TASK_VERIFYING = 2,  // gates passed, sink.finish() in flight
    DLMGR_TASK_DONE      = 3,  // terminal
    DLMGR_TASK_FAILED    = 4,  // terminal (see err code)
    DLMGR_TASK_CANCELED  = 5,  // terminal
};

// --- task error codes (task callback `err`) ------------------------------------
enum DlmgrTaskError {
    DLMGR_ERR_OK                = 0,
    DLMGR_ERR_UNSUPPORTED_RANGE = 1, // range_url answered non-206 / 416
    DLMGR_ERR_SIZE_MISMATCH     = 2, // Content-Range/Content-Length/overflow != size
    DLMGR_ERR_CRC_MISMATCH      = 3, // streaming crc32 != expected
    DLMGR_ERR_NETWORK           = 4, // connect/recv/send/HTTP status failures
    DLMGR_ERR_TIMEOUT           = 5, // CURLE_OPERATION_TIMEDOUT
    DLMGR_ERR_CANCELED          = 6,
};

// --- callbacks -------------------------------------------------------------------
// Task callback: fired on the reporter thread, only when the task's state or
// progress changed since the last 100 ms tick (terminal states are reported
// at the transition tick). Signature is fixed:
//   cb(user, task_id, state, done, total, bps, err)
typedef void (NATIVEBRIDGE_CALL* dlmgr_task_cb)(void* user, uint64_t task_id, int state,
                                                uint64_t done, uint64_t total, uint64_t bps, int err);
// Global callback: fired every ~100 ms after all task callbacks of that tick:
//   cb(user, active, done_cnt, failed_cnt, bytes_done, bytes_total, bps)
//   active = Running/Verifying count (C# uses active_count == 0 as the
//   compaction precondition), done_cnt/failed_cnt = terminal counters
//   (Canceled is counted in neither), bps = aggregate bytes/sec since the
//   previous tick. Tasks are retired from the manager right after their
//   terminal state is reported; done_cnt/failed_cnt/bytes_done/bytes_total
//   remain lifetime totals (accumulated across retirement).
typedef void (NATIVEBRIDGE_CALL* dlmgr_global_cb)(void* user, uint32_t active, uint32_t done_cnt,
                                                  uint32_t failed_cnt, uint64_t bytes_done,
                                                  uint64_t bytes_total, uint64_t bps);

// --- version / ABI ---------------------------------------------------------------
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_abi_version(void);

// --- lifecycle --------------------------------------------------------------------
// Creates the manager (global curl share with DNS + default locks is created
// here). worker_count is clamped to 1..=64; global_bps == 0 means unlimited;
// retry_count is the shared retry budget (default 3). Returns NULL on failure.
// curlw_global_init is the HOST's responsibility; dlmgr never calls it.
NATIVEBRIDGE_API void*    NATIVEBRIDGE_CALL dlmgr_create(uint32_t worker_count, uint64_t global_bps,
                                                         uint32_t retry_count);
// Spawns the worker threads and the reporter thread. Idempotent; must be
// called before the first enqueue takes effect immediately (enqueues are
// queued either way).
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_start(void* mgr);
// Adds a DNS-over-HTTPS server (CURLOPT_DOH_URL) for every worker handle.
// Call before dlmgr_start for guaranteed effect (later calls are stored but
// not applied to already-running handles).
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_add_doh_url(void* mgr, const char* url);
// Graceful shutdown: cancels all tasks, joins every worker and the reporter
// thread, releases the curl share, then CONSUMES the handle (do not use it
// again, do not call twice).
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL dlmgr_shutdown(void* mgr);

// --- tasks -------------------------------------------------------------------------
// Enqueues a task. range_url is required (first request already carries
// "Range: bytes={off}-", off=0 probes range support); full_url may be NULL or
// empty = no fallback; crc32 is the zlib-style crc32 of the whole content
// (required); priority: higher first, same priority FIFO, no preemption;
// task_bps == 0 = unlimited; sink is a registered sink handle (non-zero).
// On success returns 0 and writes *out_task_id (may be NULL); -1 on failure.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_enqueue(void* mgr, const char* range_url,
                                                          const char* full_url, const char* name,
                                                          uint64_t size, uint32_t crc32, int priority,
                                                          uint64_t task_bps, uint64_t sink,
                                                          uint64_t* out_task_id);
// Cancels one task: queued -> Canceled directly (sink never touched);
// running -> cancel flag aborts the transfer through the write callback and
// the worker finalizes with Canceled. Unknown id (including tasks already
// retired after their terminal state was reported) -> -1.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_cancel(void* mgr, uint64_t task_id);
// Cancels all tasks (queued ones become Canceled, running ones get aborted).
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_cancel_all(void* mgr);
// Stops dispatching new tasks; running tasks run to completion.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_pause(void* mgr);
// Resumes dispatching.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_resume(void* mgr);
// Global speed limit, takes effect immediately; bps == 0 = unlimited.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_set_global_speed(void* mgr, uint64_t bps);
// Registers the task callback (NULL clears it). Keep the delegate alive for
// the manager lifetime.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_set_task_callback(void* mgr, dlmgr_task_cb cb,
                                                                    void* user);
// Registers the global callback (NULL clears it). Keep the delegate alive for
// the manager lifetime.
NATIVEBRIDGE_API int      NATIVEBRIDGE_CALL dlmgr_set_global_callback(void* mgr, dlmgr_global_cb cb,
                                                                      void* user);
// Number of tasks currently Running/Verifying.
NATIVEBRIDGE_API uint32_t NATIVEBRIDGE_CALL dlmgr_active_count(void* mgr);

// --- sinks ---------------------------------------------------------------------------
// Convenience sink writing to a plain file at `path` (one OS handle per sink,
// pwrite at absolute offsets, pre-sized on prepare, truncated on abort).
// Returns a u64 sink handle for dlmgr_enqueue, 0 on failure. Release it with
// the sink release entry point only after the task reached a terminal state.
NATIVEBRIDGE_API uint64_t NATIVEBRIDGE_CALL dlmgr_sink_create_file(void* mgr, const char* path,
                                                                   uint64_t size);
// Releases a sink handle (plain-file or VFS-backed, see glue.h exports). Must
// be called exactly once per handle, only after its task reported a terminal
// state (Done/Failed/Canceled); queued-then-canceled tasks never touch the
// sink, so releasing from the task callback's terminal tick is always safe.
NATIVEBRIDGE_API void     NATIVEBRIDGE_CALL dlmgr_sink_release(uint64_t sink);

#ifdef __cplusplus
}
#endif

#endif // DLMGR_H

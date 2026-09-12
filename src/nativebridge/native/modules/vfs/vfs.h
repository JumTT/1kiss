// vfs.h — authoritative C ABI for the "vfs" module (virtual file container).
//
// This header is the SINGLE SOURCE OF TRUTH for the native<->C# contract.
// Vfs.cs mirrors these declarations one-for-one. Every function:
//   * is extern "C" (no name mangling),
//   * is decorated with NATIVEBRIDGE_API (exported),
//   * uses NATIVEBRIDGE_CALL == cdecl (matches CallingConvention.Cdecl in C#).
//
// Model (design doc §2): a VFS directory holds `header.vfs` (index, double
// buffered, never exposed to C#) and `files.vfs` (pure 4K-aligned data blob).
// Index queries are answered from the in-memory index (zero file I/O); C# maps
// files.vfs via MemoryMappedFile and reads Active entries directly. commit
// makes data visible immediately and never moves it (compaction is exclusive
// with writers), so concurrent C# reads of committed files are tear-free.
//
// Handle lifetime: vfs_open returns an opaque handle backed by a reference-
// counted Rust object; exactly one vfs_close releases it. C# MUST keep the
// handle open while any native writer (from vfs_alloc, typically wrapped by a
// download sink) is outstanding — C# 保证 sink 存活期不 close. Closing while a
// writer lives leaks the writer's slot until process exit. Compaction keeps
// the object alive internally even if close races it, but callers should not
// rely on that.
//
// Single instance per directory: opening the same directory twice yields two
// independent in-memory indexes over the same header.vfs/files.vfs and is
// UNSUPPORTED (each instance would flip the shared SuperBlock blindly) — the
// host must keep exactly one open handle per directory.
//
// Callback keep-alive: the native side stores RAW function pointers. C#
// delegates marshalled as these pointers MUST stay strongly referenced for as
// long as they are registered (see Vfs.cs — it pins them for you) and MUST be
// static + [MonoPInvokeCallback] on IL2CPP/AOT. The commit callback fires on
// whatever thread calls commit; progress/done fire on the compaction thread.
// Callbacks may re-enter read-only VFS entry points (lookup/enumerate/stat);
// they must NOT call compact (single-threaded state machine).
//
// Strings cross the ABI as NUL-terminated UTF-8 (const char*). No structs are
// passed across the ABI; results go through out parameters (all out parameters
// are optional / may be NULL unless stated otherwise).
//
// ABI versioning: bump VFS_ABI_VERSION whenever an existing signature changes
// (a C# breaking change). Purely additive exports do not bump it.
#ifndef VFS_H
#define VFS_H

#include "nativebridge.h"
#include <stddef.h>   // size_t (vfs_writer_write len / vfs_enumerate_read cap)
#include <stdint.h>

#define VFS_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// --- result codes (int, §2.4 order) -------------------------------------------
enum VfsResult {
    VFS_OK              = 0,
    VFS_IO              = 1,   // filesystem error (open/read/write/fsync failed)
    VFS_NOT_FOUND       = 2,
    VFS_ALREADY_EXISTS  = 3,   // alloc on an Active/Downloading name
    VFS_BUSY_COMPACTING = 4,   // op refused because compaction is running
    VFS_BUSY_WRITERS    = 5,   // compact refused because writers exist
    VFS_CRC_MISMATCH    = 6,   // data crc mismatch (reserved for readers)
    VFS_WRITE_OVERFLOW  = 7,   // write outside the writer's [0,size) window
    VFS_GEN_CHANGED     = 8,   // enumerate_read with a stale generation
    VFS_BUFFER_TOO_SMALL= 9,
    VFS_INVALID_ARG     = 10,  // NULL handle/string, bad UTF-8, empty name
    VFS_STATE_INVALID   = 11,  // op not valid for the object's current state
};

// --- file states (Entry.state) --------------------------------------------------
enum VfsFileState {
    VFS_FILE_ACTIVE      = 0,  // committed; safe for C# to read
    VFS_FILE_DELETED     = 1,  // soft-deleted; space reclaimed by compaction
    VFS_FILE_DOWNLOADING = 2,  // writer in flight; never visible via lookup of
                               // a *committed* read path — do not read
    VFS_FILE_BAD         = 3,  // crc mismatch found by compaction; dropped at
                               // the next Finalize
};

// --- compaction states (vfs_compact_status) -------------------------------------
enum VfsCompactState {
    VFS_COMPACT_IDLE     = 0,
    VFS_COMPACT_SCAN     = 1,
    VFS_COMPACT_MOVE     = 2,
    VFS_COMPACT_FINALIZE = 3,
};

// --- callbacks ------------------------------------------------------------------
// commit callback: fired after a writer commits (name/off/size/crc of the new
// Active entry). Fires on the committing thread.
typedef void (NATIVEBRIDGE_CALL* vfs_commit_cb)(void* user, const char* name,
                                                uint64_t off, uint64_t size, uint32_t crc);
// compaction progress: percent 0..100, name = file being copied ("" at 100%).
// Fires on the compaction thread.
typedef void (NATIVEBRIDGE_CALL* vfs_progress_cb)(void* user, int percent, const char* name);
// compaction finished: err == VFS_OK on success, else the failure code.
// Fires exactly once per vfs_compact, on the compaction thread.
typedef void (NATIVEBRIDGE_CALL* vfs_done_cb)(void* user, int err);

// --- version / ABI ---------------------------------------------------------------
NATIVEBRIDGE_API int NATIVEBRIDGE_CALL vfs_abi_version(void);

// --- open / close -----------------------------------------------------------------
// Opens (or initializes) a VFS directory (header.vfs + files.vfs created on
// demand). Recovery: SuperBlock corruption falls back to scanning both header
// regions (highest valid gen wins); leftover Downloading entries are dropped
// (their space becomes garbage); physical bytes beyond the logical size are
// ignored. Returns NULL on failure (VFS_IO / VFS_INVALID_ARG).
NATIVEBRIDGE_API void*  NATIVEBRIDGE_CALL vfs_open(const char* dir);
// Releases the handle. Pair every successful open with exactly one close.
NATIVEBRIDGE_API void   NATIVEBRIDGE_CALL vfs_close(void* h);

// --- flush -----------------------------------------------------------------------
// Explicit persistence (there is no periodic flush): fsync files.vfs, write the
// inactive header region, fsync header.vfs, then update SuperBlock.active_gen.
// A crash loses commits not yet flushed. Returns VFS_BUSY_COMPACTING during
// compaction.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_flush(void* h);

// --- write path (three-phase) ------------------------------------------------------
// Registers a Downloading entry at the logical end (4K aligned), grows
// files.vfs if needed, returns an opaque writer handle (NULL + no error code on
// failure). Same-name rules: Deleted may be replaced (old span becomes
// garbage); Active/Downloading -> VFS_ALREADY_EXISTS. Refused with
// VFS_BUSY_COMPACTING during compaction.
NATIVEBRIDGE_API void*  NATIVEBRIDGE_CALL vfs_alloc(void* h, const char* name, uint64_t size);
// Writes buf[0..len) at rel_off inside the writer's window. Sequential writes
// (rel_off == bytes written so far) stream the crc; a rel_off below the high-
// water mark is allowed (download source switch) and forces a read-back crc at
// commit; gaps or writes beyond size return VFS_WRITE_OVERFLOW.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_writer_write(void* w, uint64_t rel_off,
                                                           const void* buf, size_t len);
// Validates the window is fully written, turns the entry Active, bumps the
// generation, fires the commit callback, and frees the writer. On error the
// writer is freed and the entry aborted (its span becomes garbage).
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_writer_commit(void* w);
// Drops the entry (span becomes garbage) and frees the writer. Always VFS_OK.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_writer_abort(void* w);

// --- delete -------------------------------------------------------------------------
// Soft delete: Active -> Deleted (generation++). Already-Deleted is idempotent;
// Downloading/Bad -> VFS_STATE_INVALID; unknown name -> VFS_NOT_FOUND.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_delete(void* h, const char* name);

// --- lookup --------------------------------------------------------------------------
// In-memory index answer. All out parameters optional (may be NULL). Returns
// the entry regardless of state — readers must check *out_state ==
// VFS_FILE_ACTIVE (the anti-tear gate). VFS_BUSY_COMPACTING during compaction.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_lookup(void* h, const char* name,
                                                     uint64_t* out_off, uint64_t* out_size,
                                                     int* out_state, uint32_t* out_crc);

// --- enumerate (two-phase) ------------------------------------------------------------
// Phase 1: returns the current generation, entry count and name-blob size (out
// parameters optional). Phase 2: fills caller arrays; names_buf receives the
// concatenated NUL-terminated UTF-8 names (blob_len bytes), off/size/crc/state
// are entry-count arrays indexed in lockstep (state is a VfsFileState value).
// If the generation changed between the two calls, phase 2 returns
// VFS_GEN_CHANGED without writing anything — re-query and retry. cap < count
// -> VFS_BUFFER_TOO_SMALL.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_enumerate_query(void* h, uint64_t* out_gen,
                                                              uint32_t* out_count,
                                                              uint32_t* out_blob_len);
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_enumerate_read(void* h, uint64_t gen,
                                                             uint8_t* names_buf,
                                                             uint64_t* off, uint64_t* size,
                                                             uint32_t* crc, int* state,
                                                             size_t cap);

// --- generation / stat -----------------------------------------------------------------
// Bumped by every index change: alloc, abort, commit, delete, compaction finalize.
// C# polls this (or uses the commit callback) to know when to re-enumerate; the
// two-phase enumerate relies on it to invalidate stale generations.
NATIVEBRIDGE_API uint64_t NATIVEBRIDGE_CALL vfs_get_generation(void* h);
// Byte accounting (all out optional): logical = append cursor; physical =
// files.vfs on-disk size; total = 4K-aligned spans of ALL registered entries;
// active/deleted = spans by state; garbage = logical minus total (aborted
// spans, replaced Deleted data, dropped Downloading leftovers). Not limited
// during compaction.
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_stat(void* h, uint64_t* out_logical,
                                                   uint64_t* out_physical, uint64_t* out_total,
                                                   uint64_t* out_active, uint64_t* out_deleted,
                                                   uint64_t* out_garbage);

// --- commit callback ---------------------------------------------------------------------
// Optional; cb == NULL clears it. Keep the delegate alive for the handle
// lifetime (see keep-alive notes in the header comment).
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_set_commit_callback(void* h, vfs_commit_cb cb,
                                                                  void* user);

// --- compaction (async) ---------------------------------------------------------------------
// Runs Idle -> Scan -> Move -> Finalize on an internal thread. Refused with
// VFS_BUSY_WRITERS while any writer exists, VFS_BUSY_COMPACTING while already
// running. During compaction lookup/enumerate/alloc/delete/commit/flush return
// VFS_BUSY_COMPACTING; stat/compact_status/get_generation stay available.
// reserve_extra: bytes pre-allocated beyond the new logical size (truncate
// garbage + pre-allocate in one step). Progress 0->100 then one done callback
// (both optional). C# must re-enumerate after done (offsets may shift).
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_compact(void* h, uint64_t reserve_extra,
                                                      vfs_progress_cb progress_cb,
                                                      vfs_done_cb done_cb, void* user);
// Current state + percent (out parameters optional).
NATIVEBRIDGE_API int    NATIVEBRIDGE_CALL vfs_compact_status(void* h, int* out_state,
                                                             int* out_percent);

// --- glue-layer export (modules/glue.rs; download-into-VFS bridge) ------------
// Creates a dlmgr sink backed by this VFS: the manager streams into `name`
// through a native writer (alloc on prepare, commit on finish, abort discards).
// `h` must stay open (no vfs_close) for the whole lifetime of the returned
// sink. Returns a u64 sink handle for dlmgr_enqueue, 0 on failure. Release it
// with dlmgr_sink_release after the task's terminal state (see dlmgr.h).
NATIVEBRIDGE_API uint64_t NATIVEBRIDGE_CALL dlvfs_sink_create_for_vfs(void* h,
                                                                      const char* name,
                                                                      uint64_t size);

#ifdef __cplusplus
}
#endif

#endif // VFS_H

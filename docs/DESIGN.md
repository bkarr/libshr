# libshr Library Design Document

## Overview

libshr is a C library implementing lock-free data structures in POSIX shared memory for high-performance inter-process communication (IPC). The library is designed for concurrent access by multiple processes and threads without traditional locking mechanisms.

**Version:** 0.17.3
**Memory Layout Version:** 3 (QVERSION)

---

## File Organization

| File | Purpose |
|------|---------|
| `include/shared.h` | Public types, status codes, time macros |
| `src/shared.c` | Status code explanation function |
| `src/shared_int.h` | Internal types, atomic primitives, memory management declarations |
| `src/shared_int.c` | Shared memory management, lock-free allocation, extent handling |
| `include/shared_q.h` | Public queue API declarations |
| `src/shared_q.c` | Queue implementation (enqueue, dequeue, events, signals) |

---

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────────┐
│                     Public Queue API                            │
│   shr_q_create, shr_q_add, shr_q_remove, shr_q_monitor, etc.   │
├─────────────────────────────────────────────────────────────────┤
│                   Queue Implementation                          │
│   enq/deq, lifo_add/remove, fifo_add/remove, event handling    │
├─────────────────────────────────────────────────────────────────┤
│               Shared Memory Management                          │
│   extent management, memory allocation, lock-free lists        │
├─────────────────────────────────────────────────────────────────┤
│                  Atomic Primitives                              │
│   CAS, DWCAS, AFA, AFS (platform-specific implementations)     │
├─────────────────────────────────────────────────────────────────┤
│                 POSIX Shared Memory                             │
│   shm_open, mmap, mremap, semaphores, signals                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Module 1: Common Types (`shared.h` / `shared.c`)

### Status Codes (`sh_status_e`)

All API functions return status codes for error handling:

| Code | Value | Meaning |
|------|-------|---------|
| `SH_OK` | 0 | Success |
| `SH_RETRY` | 1 | Retry previous operation |
| `SH_ERR_EMPTY` | 2 | No items available |
| `SH_ERR_LIMIT` | 3 | Depth limit reached |
| `SH_ERR_ARG` | 4 | Invalid argument |
| `SH_ERR_NOMEM` | 5 | Not enough memory |
| `SH_ERR_ACCESS` | 6 | Permission error |
| `SH_ERR_EXIST` | 7 | Existence error |
| `SH_ERR_STATE` | 8 | Invalid state |
| `SH_ERR_PATH` | 9 | Path name problem |
| `SH_ERR_NOSUPPORT` | 10 | Operation not supported |
| `SH_ERR_SYS` | 11 | System error |
| `SH_ERR_CONFLICT` | 12 | Update conflict |
| `SH_ERR_NO_MATCH` | 13 | No match found |

### Data Types (`sh_type_e`)

Type metadata for queued items:

| Type | Description |
|------|-------------|
| `SH_VECTOR_T` | Vector of multiple types |
| `SH_STRM_T` | Unspecified byte stream |
| `SH_INTEGER_T` | Integer (length determines size) |
| `SH_FLOAT_T` | Floating point |
| `SH_ASCII_T` | ASCII string |
| `SH_UTF8_T` | UTF-8 string |
| `SH_UTF16_T` | UTF-16 string |
| `SH_JSON_T` | JSON string |
| `SH_XML_T` | XML string |
| `SH_STRUCT_T` | Binary struct |

### Time Macros

```c
timespecadd(a, b, result)   // Add two timespec values
timespecsub(a, b, result)   // Subtract two timespec values
timespeccmp(a, b, cmp)      // Compare two timespec values
```

---

## Module 2: Atomic Primitives (`shared_int.h`)

### Platform Configuration

```c
#ifdef __x86_64__
    #define SZ_SHIFT 3      // 8 bytes per slot (64-bit)
    #define REM 7           // Remainder mask
#else
    #define SZ_SHIFT 2      // 4 bytes per slot (32-bit)
    #define REM 3           // Remainder mask
#endif
```

### Atomic Type Selection

Two code paths based on compiler capabilities:

1. **C11 Atomics** (`__STDC_VERSION__ >= 201112L` and `!__STDC_NO_ATOMICS__`)
   - Uses `atomic_long` type
   - Uses `atomic_*_explicit` functions with `memory_order_relaxed`

2. **Legacy GCC Builtins** (fallback)
   - Uses `volatile long` type
   - Uses `__sync_*` builtins

### Atomic Operations

| Macro/Function | Description | Implementation |
|----------------|-------------|----------------|
| `AFA(mem, v)` | Atomic fetch-and-add | `__sync_fetch_and_add` or `atomic_fetch_add_explicit` |
| `AFS(mem, v)` | Atomic fetch-and-sub | `__sync_fetch_and_sub` or `atomic_fetch_sub_explicit` |
| `CAS(mem, old, new)` | Compare-and-swap | `__sync_bool_compare_and_swap` or `atomic_compare_exchange_weak_explicit` |
| `DWCAS(mem, old, new)` | Double-word CAS | Inline assembly (`cmpxchg16b`) on x86_64, `__sync_bool_compare_and_swap` on 32-bit |

### DWCAS Implementation (x86_64)

```c
static inline char DWCAS(volatile DWORD *mem, DWORD *old, DWORD new) {
    char r = 0;
    __asm__ __volatile__(
        "lock; cmpxchg16b (%6);"
        "setz %7; "
        : "=a" (old_l), "=d" (old_h)
        : "0" (old_l), "1" (old_h), "b" (new_l), "c" (new_h), "r" (mem), "m" (r)
        : "cc", "memory"
    );
    return r;
}
```

The DWCAS is critical for lock-free operations requiring 128-bit atomic updates (pointer + generation counter pairs).

---

## Module 3: Shared Memory Management (`shared_int.c`)

### Memory Layout Constants

```c
enum shr_constants {
    PAGE_SIZE = 4096,       // Initial/minimum allocation size
    MEM_SLOTS = 48,         // Number of memory bucket slots
};
```

### Base Header Layout (`shr_base_disp`)

The shared memory begins with a fixed header:

| Slot | Name | Purpose |
|------|------|---------|
| 0 | `TAG` | Structure identifier (e.g., "shrq") |
| 1 | `VERSION` | Memory layout version |
| 2 | `SIZE` | Current size in slots |
| 3 | `EXPAND_SIZE` | Target expansion size |
| 4 | `FREE_HEAD` | Free list head pointer |
| 5 | `FREE_HD_CNT` | Free list head generation counter |
| 6 | `DATA_ALLOC` | Next allocation position |
| 7 | `COUNT` | Item count |
| 8 | `BUFFER` | Max buffer size needed |
| 9 | `FLAGS` | Configuration flags |
| 10 | `ID_CNTR` | Unique ID/generation counter |
| 11 | `SPARE` | Reserved |
| 12 | `FREE_TAIL` | Free list tail pointer |
| 13 | `FREE_TL_CNT` | Free list tail generation counter |
| 14-109 | `MEM_BKT_*` | Memory bucket slots (48 buckets × 2 slots) |
| 110+ | `BASE` | Data area begins |

### Extent Structure

Extents track memory-mapped regions:

```c
typedef struct extent {
    struct extent *next;    // Linked list pointer
    long *array;            // Pointer to mapped memory
    long size;              // Size in bytes
    long slots;             // Size in slots
} extent_s;
```

### Base Structure

```c
typedef struct shr_base {
    char *name;             // Queue name
    extent_s *prev;         // Previous extent (for cleanup)
    extent_s *current;      // Current active extent
    atomictype accessors;   // Active accessor count
    int fd;                 // File descriptor
    int prot;               // mmap protection flags
    int flags;              // mmap flags
} shr_base_s;
```

### Memory Allocation Strategy

1. **Bump Allocation** (`alloc_new_data`)
   - Advances `DATA_ALLOC` pointer atomically using CAS
   - Expands shared memory via `ftruncate` if needed
   - Creates new extent via `mmap` when size changes

2. **Bucket-Based Reallocation** (`realloc_data_slots`, `free_data_slots`)
   - Freed memory organized by power-of-2 sizes
   - 48 buckets for sizes 4, 8, 16, ... slots
   - Uses lock-free stack per bucket
   - Reduces fragmentation by rounding up to power-of-2

3. **Allocation Flow**:
   ```
   alloc_data_slots()
       ├── Round up to power-of-2
       ├── Check bucket for freed memory (realloc_data_slots)
       │   └── Pop from bucket stack via DWCAS
       └── If none available, allocate new (alloc_new_data)
           ├── CAS to advance DATA_ALLOC
           └── Expand shared memory if needed
   ```

### Lock-Free Linked List Operations

Used for free lists and queue management:

**Add to End** (`add_end`):
```
1. Allocate generation counter via AFA
2. Initialize new node (points to self)
3. Loop:
   a. Read current tail
   b. Try DWCAS to update tail->next
   c. Try DWCAS to update tail pointer
   d. Help other threads complete if needed
```

**Remove from Front** (`remove_front`):
```
1. Verify ref != tail (not last element)
2. Read next pointer from head
3. DWCAS to swing head to next (with generation increment)
4. Clear removed node memory
5. Return slot of removed node
```

### Dynamic Expansion

When more memory is needed:

1. `expand()` calculates new size (page-aligned)
2. `ftruncate()` extends the shared memory file
3. `resize_extent()` creates new extent via `mmap`
4. New extent linked to end of extent list
5. `base->current` updated atomically via CAS

Previous extents are cleaned up by `release_prev_extents()` when no accessors remain.

---

## Module 4: Queue Implementation (`shared_q.c`)

### Queue Header Layout (`shr_q_disp`)

Extends base header with queue-specific slots:

| Slot | Name | Purpose |
|------|------|---------|
| BASE+0 | `EVENT_TAIL` | Event queue tail |
| BASE+1 | `EVENT_TL_CNT` | Event queue tail counter |
| BASE+2 | `TAIL` | Item queue tail |
| BASE+3 | `TAIL_CNT` | Item queue tail counter |
| BASE+4 | `TS_SEC` | Last add timestamp (seconds) |
| BASE+5 | `TS_NSEC` | Last add timestamp (nanoseconds) |
| BASE+6 | `LISTEN_PID` | Arrival listener PID |
| BASE+7 | `LISTEN_SIGNAL` | Arrival listener signal |
| BASE+8 | `LIMIT_SEC` | Time limit (seconds) |
| BASE+9 | `LIMIT_NSEC` | Time limit (nanoseconds) |
| BASE+10 | `EVENT_HEAD` | Event queue head |
| BASE+11 | `EVENT_HD_CNT` | Event queue head counter |
| BASE+12 | `NOTIFY_PID` | Event monitor PID |
| BASE+13 | `NOTIFY_SIGNAL` | Event monitor signal |
| BASE+14 | `HEAD` | Item queue head |
| BASE+15 | `HEAD_CNT` | Item queue head counter |
| BASE+16 | `EMPTY_SEC` | Last empty timestamp (seconds) |
| BASE+17 | `EMPTY_NSEC` | Last empty timestamp (nanoseconds) |
| BASE+18-21 | `DEQ_SEM` | Dequeue semaphore |
| BASE+22-25 | `ENQ_SEM` | Enqueue semaphore |
| BASE+26 | `CALL_PID` | Call listener PID |
| BASE+27 | `CALL_SIGNAL` | Call listener signal |
| BASE+28 | `CALL_BLOCKS` | Blocked remove count |
| BASE+29 | `CALL_UNBLOCKS` | Unblocked remove count |
| BASE+30 | `TARGET_SEC` | CoDel target delay (seconds) |
| BASE+31 | `TARGET_NSEC` | CoDel target delay (nanoseconds) |
| BASE+32 | `STACK_HEAD` | LIFO stack head |
| BASE+33 | `STACK_HD_CNT` | LIFO stack head counter |
| BASE+34 | `LEVEL` | Depth event level |
| BASE+35 | `MAX_DEPTH` | Maximum queue depth |
| BASE+36-39 | `EVNT_SEM` | Event semaphore |
| BASE+40-53 | `AVAIL` | Available slots |
| BASE+54 | `HDR_END` | End of header |

### Queue Structure

```c
struct shr_q {
    BASEFIELDS;         // Inherited from shr_base_s
    sq_mode_e mode;     // Access mode (read/write/both)
};
```

### Queue Node Layout

Each queue node occupies 4 slots (`NODE_SIZE`):

| Offset | Content |
|--------|---------|
| 0 | Next node pointer |
| 1 | Generation counter |
| 2 | Event type (for event nodes) |
| 3 | Data slot pointer |

### Data Item Layout

| Offset | Name | Content |
|--------|------|---------|
| 0 | `DATA_SLOTS` | Total slots used |
| 1 | `TM_SEC` | Timestamp seconds |
| 2 | `TM_NSEC` | Timestamp nanoseconds |
| 3 | `UID` | Unique ID |
| 4 | `TYPE` | Data type |
| 5 | `VEC_CNT` | Vector count |
| 6 | `DATA_LENGTH` | Data length in bytes |
| 7+ | `DATA_HDR` | Actual data begins |

### Enqueue Flow (`shr_q_add`)

```
1. Validate arguments and mode
2. guard_q_memory() - increment accessor count
3. enq_gate_try() - acquire semaphore (non-blocking)
   - Returns SH_ERR_LIMIT if queue full
4. copy_value() - allocate data slots and copy value
   - calc_data_slots() - calculate slots needed
   - alloc_data_slots() - allocate memory
   - Copy data with timestamp and unique ID
5. enq_data() - add to queue
   - alloc_idx_slots() - allocate queue node
   - Check if adaptive LIFO enabled and count >= level
     - Yes: lifo_add() - push to stack
     - No: fifo_add() - append to tail via add_end()
   - AFA to increment COUNT
   - post_process_enq() - handle events
6. deq_release_gate() - release dequeue semaphore
7. check_for_level_event() - generate level event if needed
8. unguard_q_memory() - decrement accessor count
```

### Dequeue Flow (`shr_q_remove`)

```
1. Validate arguments and mode
2. guard_q_memory() - increment accessor count
3. deq_gate_try() - acquire semaphore (non-blocking)
   - Returns SH_ERR_EMPTY if queue empty
   - Signals CALL listener if registered
4. deq() - remove from queue
   - Loop until successful:
     - If STACK_HEAD != 0: lifo_remove() from stack
     - Else: fifo_remove() from head
   - safely_copy_data() - copy to caller's buffer
   - post_process_deq() - free slots, handle events
5. enq_release_gate() - release enqueue semaphore
6. unguard_q_memory() - decrement accessor count
7. Return sq_item_s with data
```

### FIFO vs LIFO Removal

**FIFO** (`fifo_remove`):
- Removes from head of linked list
- Standard queue behavior

**LIFO** (`lifo_remove`):
- Removes from separate stack (STACK_HEAD)
- Used when adaptive LIFO enabled and depth exceeds level
- Implements CoDel-style traffic spike handling

### Configuration Flags

| Flag | Value | Purpose |
|------|-------|---------|
| `FLAG_ACTIVATED` | 1 | Queue has received first item |
| `FLAG_DISCARD_EXPIRED` | 2 | Discard items exceeding time limit |
| `FLAG_LIFO_ON_LEVEL` | 4 | Enable adaptive LIFO at depth level |
| `FLAG_EVNT_INIT` | 8 | Subscribe to initial add event |
| `FLAG_EVNT_LIMIT` | 16 | Subscribe to max depth event |
| `FLAG_EVNT_TIME` | 32 | Subscribe to time limit event |
| `FLAG_EVNT_LEVEL` | 64 | Subscribe to depth level event |
| `FLAG_EVNT_EMPTY` | 128 | Subscribe to queue empty event |
| `FLAG_EVNT_NONEMPTY` | 256 | Subscribe to add-to-empty event |

### Event System

Events are queued separately from data items:

1. `add_event()` - Creates event node and appends to event queue
2. `shr_q_event()` / `shr_q_event_timedwait()` - Retrieves next event
3. Events are one-shot (subscription cleared when event fires)
4. Signal sent to registered monitor process

### Signal Notifications

Three notification channels:

| Function | Signal Slot | Purpose |
|----------|-------------|---------|
| `shr_q_monitor()` | `NOTIFY_*` | Queue events (empty, full, level) |
| `shr_q_listen()` | `LISTEN_*` | Item arrival on empty queue |
| `shr_q_call()` | `CALL_*` | Remove blocking on empty queue |

Signals sent via `sigqueue()` with value payload.

### Semaphore-Based Flow Control

Three semaphores manage queue access:

| Semaphore | Initial Value | Purpose |
|-----------|---------------|---------|
| `DEQ_SEM` | 0 | Blocks dequeue when empty |
| `ENQ_SEM` | max_depth | Blocks enqueue when full |
| `EVNT_SEM` | 0 | Blocks event wait when no events |

Operations:
- Enqueue: `sem_wait(ENQ_SEM)`, then `sem_post(DEQ_SEM)`
- Dequeue: `sem_wait(DEQ_SEM)`, then `sem_post(ENQ_SEM)`

### CoDel Algorithm Support

Controlled Delay (CoDel) for traffic spike smoothing:

1. Set target delay via `shr_q_target_delay()`
2. Set time limit via `shr_q_timelimit()`
3. When active:
   - If queue was empty within limit interval: use limit for expiration
   - Otherwise: use target delay for expiration
4. Combined with adaptive LIFO (`shr_q_limit_lifo()`) for full CoDel behavior

---

## Concurrency Model

### Lock-Free Guarantees

The implementation provides **lock-free** progress:
- At least one thread makes progress
- No thread blocks indefinitely waiting for another
- Uses retry loops with CAS/DWCAS

### ABA Problem Prevention

The ABA problem (where a value changes A→B→A) is prevented by:
- Pairing pointers with generation counters in DWORD structures
- Using DWCAS to atomically update both pointer and counter
- Incrementing counter on every modification

### Memory Ordering

**Current Implementation:**
- Uses `memory_order_relaxed` for C11 atomics
- Works correctly on x86_64 (TSO memory model)
- **Warning:** Insufficient for ARM (requires acquire/release semantics)

See `ARM_GRAVITON_PORTING_GUIDE.md` for porting considerations.

### Multi-Process Safety

- Semaphores initialized with `pshared=1` for inter-process use
- Shared memory allows concurrent access from multiple processes
- Each process maintains its own extent list (private mappings to shared data)
- Accessor counting prevents premature cleanup of old extents

---

## Memory Management Details

### Allocation Sizing

Data allocations are rounded up to powers of 2:
```c
if (__builtin_popcountl(slots) > 1) {
    slots = 1 << (LONG_BIT - __builtin_clzl(slots));
}
```

### Bucket Selection

Freed memory goes to bucket based on size:
```c
long index = __builtin_ctzl(count) - 2;  // ctz for power-of-2 detection
long bucket = MEM_BKT_START + (index * 2);
```

### Stack-Based Free Lists

Each bucket is a lock-free stack:
- Push: DWCAS to update bucket head
- Pop: DWCAS to swing head to next

---

## Public API Summary

### Lifecycle

| Function | Purpose |
|----------|---------|
| `shr_q_create()` | Create new queue |
| `shr_q_open()` | Open existing queue |
| `shr_q_close()` | Close queue handle |
| `shr_q_destroy()` | Destroy queue completely |

### Data Operations

| Function | Blocking | Purpose |
|----------|----------|---------|
| `shr_q_add()` | No | Add item |
| `shr_q_add_wait()` | Yes | Add item (block if full) |
| `shr_q_add_timedwait()` | Timeout | Add item (timeout) |
| `shr_q_addv()` | No | Add vector |
| `shr_q_addv_wait()` | Yes | Add vector (block if full) |
| `shr_q_addv_timedwait()` | Timeout | Add vector (timeout) |
| `shr_q_remove()` | No | Remove item |
| `shr_q_remove_wait()` | Yes | Remove item (block if empty) |
| `shr_q_remove_timedwait()` | Timeout | Remove item (timeout) |

### Monitoring

| Function | Purpose |
|----------|---------|
| `shr_q_monitor()` | Register for event signals |
| `shr_q_listen()` | Register for arrival signals |
| `shr_q_call()` | Register for block signals |
| `shr_q_event()` | Get next event (non-blocking) |
| `shr_q_event_timedwait()` | Get next event (timeout) |
| `shr_q_subscribe()` | Subscribe to event type |
| `shr_q_unsubscribe()` | Unsubscribe from event type |

### Configuration

| Function | Purpose |
|----------|---------|
| `shr_q_level()` | Set depth event level |
| `shr_q_timelimit()` | Set time limit |
| `shr_q_target_delay()` | Set CoDel target delay |
| `shr_q_discard()` | Enable/disable expired item discard |
| `shr_q_limit_lifo()` | Enable/disable adaptive LIFO |

### Queries

| Function | Purpose |
|----------|---------|
| `shr_q_count()` | Get current item count |
| `shr_q_buffer()` | Get recommended buffer size |
| `shr_q_last_empty()` | Get last empty timestamp |
| `shr_q_exceeds_idle_time()` | Check idle time |
| `shr_q_will_discard()` | Check discard setting |
| `shr_q_will_lifo()` | Check LIFO setting |
| `shr_q_is_subscribed()` | Check event subscription |
| `shr_q_is_valid()` | Validate queue name |
| `shr_q_call_count()` | Get blocked call count |

---

## Validation and Identification

### Queue Tag

Queues are identified by a 4-byte tag:
- 64-bit: `"shrq"`
- 32-bit: `"sq32"`

### Version Checking

`is_valid_queue()` verifies:
1. TAG matches expected value
2. VERSION equals QVERSION (3)

---

## Error Handling Patterns

### Transient Failures

Many internal operations retry in loops:
```c
while (!CAS(&array[slot], &old, new)) {
    old = array[slot];  // Reload and retry
}
```

### Memory Exhaustion

When allocation fails:
1. Return `SH_ERR_NOMEM`
2. Caller should retry or handle gracefully
3. No partial state left behind

### Invalid State Detection

Queue corruption detected by:
- Version mismatch
- Tag mismatch
- Semaphore errors (`EINVAL`)

Returns `SH_ERR_STATE` for investigation.

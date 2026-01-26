#ifndef SHAREDINT_H_
#define SHAREDINT_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <shared.h>


/*============================================================================
    Architecture Validation
============================================================================*/
#if !defined(__x86_64__) && !defined(__aarch64__) && !defined(__i386__)
    #warning "Untested architecture: only x86_64, i386, and aarch64 are validated"
#endif

/*============================================================================
    Type Definitions
============================================================================*/

/* Unified type - __atomic_* builtins work directly on volatile long */
typedef volatile long atomictype;

/* Both x86_64 and aarch64 are 64-bit architectures with 8-byte longs */
#if defined(__x86_64__) || defined(__aarch64__)
#define SZ_SHIFT 3
#define REM 7
#else
/* 32-bit fallback (i386) */
#define SZ_SHIFT 2
#define REM 3
#endif

#define LONG_BIT (CHAR_BIT * sizeof(long))

// define unchanging file system related constants
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define SHR_OBJ_DIR "/dev/shm/"


// define useful integer constants (mostly sizes and offsets)
enum shr_constants
{
    PAGE_SIZE = 4096,       // initial size of memory mapped file
    MEM_SLOTS = 48,         // number of memory bucket allocation slots
};



// define shared data structure base offsets
enum shr_base_disp
{

    TAG = 0,                                        // queue identifier tag
    VERSION,                                        // implementation version number
    SIZE,                                           // size of queue array
    EXPAND_SIZE,                                    // size for current expansion
    FREE_HEAD,                                      // free node list head
    FREE_HD_CNT,                                    // free node head counter
    DATA_ALLOC,                                     // next available data allocation slot
    COUNT,                                          // number of items in structure
    BUFFER,                                         // max buffer size needed to read
    FLAGS,                                          // configuration flag values
    ID_CNTR,                                        // unique id/generation counter
    SPARE,                                          // spare slot
    FREE_TAIL,                                      // free node list tail
    FREE_TL_CNT,                                    // free node tail counter
    MEM_BKT_START,                                  // start of free memory bucket slots
    MEM_BKT_END = (MEM_BKT_START + (MEM_SLOTS * 2)),    // allocate space for free memory bucket slots
    BASE = MEM_BKT_END

};


typedef unsigned long ulong;

/*
 * DWORD: 128-bit double-word for atomic operations
 *
 * Alignment requirements:
 * - x86_64: CMPXCHG16B requires 16-byte alignment
 * - ARM64: CASP (LSE) requires 16-byte alignment
 *
 * The __int128 member enables unified atomic operations via __atomic_* builtins
 */
typedef union {
    struct {
        atomictype low;
        atomictype high;
    };
    __int128 full;  /* For unified 128-bit atomic access */
} __attribute__((aligned(16))) DWORD;


/*
    reference to critbit trie node
*/
typedef struct idx_ref idx_ref_s;


/*
    internal node of critbit trie index
*/
typedef struct idx_node idx_node_s;


/*
    leaf node of critbit trie index
*/
typedef struct idx_leaf idx_leaf_s;


/*
    structure for managing mmapped data
*/
typedef struct extent
{

    struct extent *next;
    long *array;
    long size;
    long slots;

} extent_s;

/*
    view structure
*/
typedef struct view
{

    sh_status_e status;
    long slot;
    extent_s *extent;

} view_s;


#define BASEFIELDS          \
    char *name;             \
    extent_s *prev;         \
    extent_s *current;      \
    atomictype accessors;   \
    int fd;                 \
    int prot;               \
    int flags


/*
    base structure
*/
typedef struct shr_base
{

    BASEFIELDS;

} shr_base_s;



/*============================================================================
    Unified Atomic Operations

    These use __atomic_* builtins which work on both x86_64 and ARM64.

    Memory Ordering:
    - __ATOMIC_ACQ_REL provides acquire-release semantics
    - On x86_64: Generates same instructions as __sync_* (LOCK prefix = full barrier)
    - On ARM64: Generates proper barrier instructions (CASPAL, LDADDAL with LSE)

    This unified approach:
    - Eliminates architecture-specific #ifdef blocks
    - Guarantees correctness on ARM's relaxed memory model
    - Has zero performance cost on x86_64 (identical generated code)
============================================================================*/

/*
 * Atomic Fetch-Add
 * x86_64: Generates LOCK XADD
 * ARM64:  Generates LDADDAL (LSE) or LDAXR/STLXR loop
 */
#define AFA(mem, v) __atomic_fetch_add(mem, v, __ATOMIC_ACQ_REL)

/*
 * Atomic Fetch-Sub
 * x86_64: Generates LOCK XADD (with negated value)
 * ARM64:  Generates LDADDAL (LSE) with negated value
 */
#define AFS(mem, v) __atomic_fetch_sub(mem, v, __ATOMIC_ACQ_REL)

/*
 * Compare-And-Swap (64-bit)
 * x86_64: Generates LOCK CMPXCHG
 * ARM64:  Generates CASAL (LSE) or LDAXR/STLXR loop
 *
 * Returns: 1 on success, 0 on failure
 * On failure, *old is updated with the current value
 */
static inline char CAS(
    atomictype *mem,
    long *old,
    long new
) {
    return __atomic_compare_exchange_n(
        mem,
        old,
        new,
        0,                  /* strong: don't allow spurious failures */
        __ATOMIC_ACQ_REL,   /* success memory order: acquire-release */
        __ATOMIC_ACQUIRE    /* failure memory order: acquire */
    );
}

/*
 * Double-Word Compare-And-Swap (128-bit)
 * x86_64: Generates LOCK CMPXCHG16B (requires -mcx16)
 * ARM64:  Generates CASPAL (LSE) or LDXP/STXP loop
 *
 * Note: The DWORD union must be 16-byte aligned for both architectures.
 *
 * Returns: 1 on success, 0 on failure
 * On failure, *old is updated with the current value
 */
static inline char DWCAS(
    volatile DWORD *mem,
    DWORD *old,
    DWORD new
) {
    return __atomic_compare_exchange_n(
        &mem->full,
        &old->full,
        new.full,
        0,                  /* strong */
        __ATOMIC_ACQ_REL,   /* success memory order */
        __ATOMIC_ACQUIRE    /* failure memory order */
    );
}

/*============================================================================
    Spin-Wait Pause Hint

    Signals to the CPU that this is a spin-wait loop, reducing power
    consumption and improving performance by avoiding memory order violations.
============================================================================*/
#if defined(__x86_64__) || defined(__i386__)
    #define SPIN_PAUSE() __builtin_ia32_pause()
#elif defined(__aarch64__)
    #define SPIN_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define SPIN_PAUSE() ((void)0)
#endif


extern sh_status_e convert_to_status(
    int err                 // errno value
);

extern sh_status_e validate_name(
    char const * const name
);


extern sh_status_e validate_existence(
    char const * const name,        // name string of shared memory file
    size_t *size                    // pointer to size field -- possibly NULL
);

extern sh_status_e create_base_object(
    shr_base_s **base,      // address of base struct pointer -- not NULL
    size_t size,            // size of structure to allocate
    char const * const name,// name of q as a null terminated string -- not NULL
    char const * const tag, // tag to initialize base shared memory structure
    int tag_len,            // length of tag
    long version            // version for memory layout
);

extern void prime_list(

    shr_base_s *base,           // pointer to base struct -- not NULL
    long slot_count,            // size of item in array slots
    long head,                  // queue head slot number
    long head_counter,          // queue head gen counter
    long tail,                  // queue tail slot number
    long tail_counter           // queue tail gen counter

);

extern void init_data_allocator(
    shr_base_s *base,   // pointer to base struct -- not NULL
    long start          // start location for data allocations
);


extern bool set_flag(
    long *array,
    long indicator
);


extern bool clear_flag(
    long *array,
    long indicator
);


extern void update_buffer_size(
    long *array,
    long space,
    long vcnt
);


extern view_s resize_extent(
    shr_base_s *base,   // pointer to base struct -- not NULL
    extent_s *extent    // pointer to working extent -- not NULL
);


extern view_s expand(
    shr_base_s *base,   // pointer to base struct -- not NULL
    extent_s *extent,   // pointer to current extent -- not NULL
    long slots          // number of slots to allocate
);


extern view_s insure_in_range(
    shr_base_s *base,   // pointer to base struct -- not NULL
    long start          // starting slot
);


extern void add_end(
    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot,          // slot reference
    long tail           // tail slot of list
);


extern long remove_front(
    shr_base_s *base,   // pointer to base struct -- not NULL
    long ref,           // expected slot number -- 0 if no interest
    long gen,           // generation count
    long head,          // head slot of list
    long tail           // tail slot of list
);


extern view_s alloc_idx_slots(
    shr_base_s *base    // pointer to base struct -- not NULL
);


extern view_s alloc_new_data(
    shr_base_s *base,    // pointer to base struct -- not NULL
    long slots
);


extern sh_status_e free_data_slots(
    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot           // start of slot range
);


extern view_s alloc_data_slots(
    shr_base_s *base,   // pointer to base struct -- not NULL
    long slots          // number of slots to allocate
);

extern void release_prev_extents(
    shr_base_s *base    // pointer to base struct -- not NULL
);


extern sh_status_e perform_name_validations(
    char const * const name,        // name string of shared memory file
    size_t *size                    // pointer to size field -- possibly NULL
);

extern sh_status_e release_mapped_memory(
    shr_base_s **base       // address of base struct pointer-- not NULL
);

extern sh_status_e map_shared_memory(
    shr_base_s **base,          // address of base struct pointer-- not NULL
    char const * const name,    // name as null terminated string -- not NULL
    size_t size                 // size of shared memory
);


extern void close_base(
    shr_base_s *base    // pointer to base struct -- not NULL
);

#endif // SHAREDINT_H_

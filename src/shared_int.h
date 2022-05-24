#ifndef SHAREDINT_H_
#define SHAREDINT_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <shared.h>


#if (__STDC_VERSION__ >= 201112L)
#include <stdatomic.h>
#endif

#if ((__STDC_VERSION__ < 201112L) || __STDC_NO_ATOMICS__)
typedef volatile long atomictype;
#else
typedef atomic_long atomictype;
#endif

#ifdef __x86_64__
#define SZ_SHIFT 3
#define REM 7
#else
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
    TSTACK_DEPTH = 16,      // depth of stack for critbit trie search
};



// define shared data structure base offsets
enum shr_base_disp
{

    TAG = 0,                        // queue identifier tag
    VERSION,                        // implementation version number
    SIZE,                           // size of queue array
    EXPAND_SIZE,                    // size for current expansion
    FREE_HEAD,                      // free node list head
    FREE_HD_CNT,                    // free node head counter
    DATA_ALLOC,                     // next available data allocation slot
    COUNT,                          // number of items in structure
    ROOT_FREE,                      // root of free data index
    ROOT_FREE_CNT,                  // free data root version counter
    BUFFER,                         // max buffer size needed to read
    FLAGS,                          // configuration flag values
    ID_CNTR,                        // unique id/generation counter
    SPARE,                          // spare slot
    FREE_TAIL,                      // free node list tail
    FREE_TL_CNT,                    // free node tail counter
    BASE

};


typedef unsigned long ulong;

typedef struct {

    atomictype low;
    atomictype high;

} DWORD;


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



#if ((__STDC_VERSION__ < 201112L) || __STDC_NO_ATOMICS__)

#define AFS(mem, v) __sync_fetch_and_sub(mem, v)
#define AFA(mem, v) __sync_fetch_and_add(mem, v)


/*
    CAS -- atomic compare and swap

    Note:  Use atomic builtins because cmpxchg instructions clobber ebx register
    which is PIC register, so using builtins to be safe
*/
static inline char CAS(

    atomictype *mem,
    atomictype *old,
    atomictype new

)   {

    return __sync_bool_compare_and_swap((long*)mem, *(long*)old, new);

}


#ifdef __x86_64__

/*
    DWCAS -- atomic double word compare and swap (64 bit)
*/
static inline char DWCAS(

    volatile DWORD *mem,
    DWORD *old,
    DWORD new

)   {

    uint64_t  old_h = old->high, old_l = old->low;
    uint64_t  new_h = new.high, new_l = new.low;

    char r = 0;
    __asm__ __volatile__("lock; cmpxchg16b (%6);"
    "setz %7; "
    : "=a" (old_l),
    "=d" (old_h)
    : "0" (old_l),
    "1" (old_h),
    "b" (new_l),
    "c" (new_h),
    "r" (mem),
    "m" (r)
    : "cc", "memory");
    return r;

}

#else

/*
    DWCAS -- atomic double word compare and swap (32 bit)
*/

static inline char DWCAS(

    volatile DWORD *mem,
    DWORD *old,
    DWORD new

)   {

    return __sync_bool_compare_and_swap((long long*)mem, *(long long*)old, \
                                        *(long long*)&new);
}

#endif

#else

#define AFS(mem, v) atomic_fetch_sub_explicit((atomictype *)mem, v, \
                                              memory_order_relaxed)
#define AFA(mem, v) atomic_fetch_add_explicit((atomictype *)mem, v, \
                                              memory_order_relaxed)
#define CAS(val, old, new) atomic_compare_exchange_weak_explicit(   \
            (atomic_long*)val, (atomic_long*)old, (atomic_long)new, \
            memory_order_relaxed, memory_order_relaxed)
#define DWCAS(val, old, new) atomic_compare_exchange_weak_explicit(val, old, \
              new, memory_order_relaxed, memory_order_relaxed)

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


extern view_s realloc_pooled_mem(

    shr_base_s *base,           // pointer to base struct -- not NULL
    long slot_count,            // size as number of slots
    long head,                  // list head slot
    long head_counter,          // list head counter slot
    long tail                   // list tail slot

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

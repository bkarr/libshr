/*
The MIT License (MIT)

Copyright (c) 2015-2017 Bryan Karr

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <shared_q.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#if (__STDC_VERSION__ >= 201112L)
#include <stdatomic.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


typedef unsigned long ulong;
#ifdef __x86_64__
typedef int32_t halfword;
#define SZ_SHIFT 3
#define DELTA UINT_MAX
#define REM 7
#define SHRQ "shrq"
#else
typedef int16_t halfword;
#define SZ_SHIFT 2
#define DELTA USHRT_MAX
#define REM 3
#define SHRQ "sq32"
#endif


#if ((__STDC_VERSION__ < 201112L) || __STDC_NO_ATOMICS__)
typedef volatile long atomictype;
#else
typedef atomic_long atomictype;
#endif


// define unchanging file system related constants
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define SHR_OBJ_DIR "/dev/shm/"

// define functional flags
#define FLAG_ACTIVATED 1
#define FLAG_DISCARD_EXPIRED 2    // discard items that exceed timelimit
#define FLAG_LIFO_ON_LEVEL 4      // enable adaptive LIFO based on depth level
#define FLAG_EVNT_INIT 8          // disable event first item added to queue
#define FLAG_EVNT_LIMIT 16        // disable event max depth reached
#define FLAG_EVNT_TIME 32         // disable event max time limit reached
#define FLAG_EVNT_LEVEL 64        // disable event depth level reached
#define FLAG_EVNT_EMPTY 128       // disable event last item on queue removed
#define FLAG_EVNT_NONEMPTY 256    // disable event item added to empty queue

// define useful time related macros
#define timespecadd(a, b, result)                           \
    do {                                                    \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;       \
        (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;    \
        if ((result)->tv_nsec >= 1000000000L) {             \
            (result)->tv_sec++;                             \
            (result)->tv_nsec -= 1000000000L;               \
        }                                                   \
    } while(0)

#define timespecsub(a, b, result)                           \
  do {                                                      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;           \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;        \
    if ((result)->tv_nsec < 0) {                            \
      --(result)->tv_sec;                                   \
      (result)->tv_nsec += 1000000000;                      \
    }                                                       \
  } while (0)

#define timespeccmp(a, b, cmp)                              \
    (((a)->tv_sec == (b)->tv_sec) ?                         \
    ((a)->tv_nsec cmp (b)->tv_nsec) :                       \
    ((a)->tv_sec cmp (b)->tv_sec))

// define useful integer constants (mostly sizes and offsets)
enum shr_q_constants
{
    PAGE_SIZE = 4096,       // initial size of memory mapped file for queue
    NODE_SIZE = 4,          // node slot count
    DATA_HDR = 6,           // data header
    EVENT_OFFSET = 2,       // offset in node for event for queued item
    VALUE_OFFSET = 3,       // offset in node for data slot for queued item
    DATA_SLOTS = 0,         // total data slots (including header)
    TM_SEC = 1,             // offset for data timestamp seconds value
    TM_NSEC = 2,            // offset for data timestamp nanoseconds value
    TYPE = 3,               // offset of type indicator
    VEC_CNT = 4,            // offset for vector count
    DATA_LENGTH = 5,        // offset for data length
    TSTACK_DEPTH = 32,      // depth of stack for critbit trie search
    TSTACK_BRANCHES = 16,   // number of branches to search for larger memory
};

// define queue header slot offsets
enum shr_q_disp
{
    TAG = 0,                        // queue identifier tag
    VERSION,                        // implementation version number
    SIZE,                           // size of queue array
    COUNT,                          // number of items on queue
    EVENT_TAIL,                     // event queue tail
    EVENT_TL_CNT,                   // event queue tail counter
    TAIL,                           // item queue tail
    TAIL_CNT,                       // item queue tail counter
    FREE_HEAD,                      // free node list head
    FREE_HD_CNT,                    // free node head counter
    TS_SEC,                         // timestamp of last add in seconds
    TS_NSEC,                        // timestamp of last add in nanoseconds
    LISTEN_PID,                     // arrival notification process id
    LISTEN_SIGNAL,                  // arrival notification signal
    DATA_ALLOC,                     // next available data allocation slot
    SPARE,                          // spare slot
    EVENT_HEAD,                     // event queue head
    EVENT_HD_CNT,                   // event queue head counter
    ROOT_FREE,                      // root of free data index
    ROOT_RDX,                       // radix info for root
    HEAD,                           // item queue head
    HEAD_CNT,                       // item queue head counter
    FREE_TAIL,                      // free node list tail
    FREE_TL_CNT,                    // free node tail counter
    EMPTY_SEC,                      // time q last empty in seconds
    EMPTY_NSEC,                     // time q last empty in nanoseconds
    LIMIT_SEC,                      // time limit interval in seconds
    LIMIT_NSEC,                     // time limit interval in nanoseconds
    NOTIFY_PID,                     // event notification process id
    NOTIFY_SIGNAL,                  // event notification signal
    READ_SEM,                       // read semaphore
    WRITE_SEM = (READ_SEM + 4),     // write semaphore
    IO_SEM = (WRITE_SEM + 4),       // i/o semaphore
    CALL_PID = (IO_SEM +4),         // demand call notification process id
    CALL_SIGNAL,                    // demand call notification signal
    CALL_BLOCKS,                    // count of blocked remove calls
    CALL_UNBLOCKS,                  // count of unblocked remove calls
    TARGET_SEC,                     // target CoDel delay in seconds
    TARGET_NSEC,                    // target CoDel time limit in nanoseconds
    STACK_HEAD,                     // head of stack for adaptive LIFO
    STACK_HD_CNT,                   // head of stack counter
    FLAGS,                          // configuration flag values
    LEVEL,                          // queue depth event level
    BUFFER,                         // max buffer size needed to read
    AVAIL,                          // next avail free slot
    HDR_END = (AVAIL + 11),         // end of queue header
};


/*
    reference to critbit trie node
*/
typedef struct idx_ref
{
    long next;
    union {
        struct {
            halfword byte;
            int8_t  flag;
            uint8_t bits;
        };
        ulong diff;
    };
} idx_ref_s;


/*
    internal node of critbit trie index
*/
typedef struct idx_node
{
    idx_ref_s child[2];
} idx_node_s;


/*
    leaf node of critbit trie index
*/
typedef struct idx_leaf
{
    union
    {
        uint8_t key[sizeof(long)];
        long count;
    };
    long pad;
    long allocs;
    long allocs_count;
} idx_leaf_s;


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

*/
typedef struct view
{
    sh_status_e status;
    long slot;
    extent_s *extent;
} view_s;

/*
    base queue structure
*/
struct shr_q
{
    sq_mode_e mode;
    int fd;
    int prot;
    int flags;
    char *name;
    extent_s *prev;
    extent_s *current;
    atomictype accessors;
};


typedef struct {
    ulong low;
    ulong high;
} DWORD;

static char *status_str[] = {
    "success",
    "retry",
    "no items on queue",
    "depth limit reached",
    "invalid argument",
    "not enough memory to satisfy request",
    "permission error",
    "existence error",
    "invalid state",
    "problem with path name",
    "required operation not supported",
    "system error",
    "invalid status code for explain"
};

static void *null = NULL;

static view_s insure_in_range(shr_q_s *q, long start, long slots);

/*==============================================================================

    private functions

==============================================================================*/


#if (__STDC_VERSION__ < 201112L)

#define AFS(mem, v) __sync_fetch_and_sub(mem, v)
#define AFA(mem, v) __sync_fetch_and_add(mem, v)


/*
    CAS -- atomic compare and swap

    Note:  Use atomic builtins because cmpxchg instructions clobber ebx register
    which is PIC register, so using builtins to be safe
*/
static inline char CAS(
    volatile long *mem,
    volatile long *old,
    long new
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
    return __sync_bool_compare_and_swap((long long*)mem, *(long long*)old,
        *(long long*)&new);
}

#endif

#else

#define AFS(mem, v) atomic_fetch_sub_explicit(mem, v, memory_order_relaxed)
#define AFA(mem, v) atomic_fetch_add_explicit(mem, v, memory_order_relaxed)
#define CAS(val, old, new) atomic_compare_exchange_weak_explicit(val, old, new,\
    memory_order_relaxed, memory_order_relaxed)
#define DWCAS(val, old, new) atomic_compare_exchange_weak_explicit(val, old, \
    new, memory_order_relaxed, memory_order_relaxed)

#endif


static sh_status_e convert_to_status(
    int err                 // errno value
)   {
    switch(err) {
    case EINVAL :
        return SH_ERR_ARG;
    case EPERM :
    case EACCES :
        return SH_ERR_ACCESS;
    case EEXIST :
        return SH_ERR_EXIST;
    case ENOMEM :
        return SH_ERR_NOMEM;
    case EBADF :
    case ENOENT :
    case ELOOP :
    case ENOTDIR :
    case ENAMETOOLONG :
        return SH_ERR_PATH;
    case EIO :
    default :
        return SH_ERR_SYS;
    }
}


static inline bool set_flag(
    long *array,
    long indicator
)   {
    volatile long prev = (volatile long)array[FLAGS];
    while (!(prev & indicator)) {
        if (CAS(&array[FLAGS], &prev, prev | indicator)) {
            return true;
        }
        prev = (volatile long)array[FLAGS];
    }
    return false;
}



static inline bool clear_flag(
    long *array,
    long indicator
)   {
    long mask = ~indicator;
    volatile long prev = (volatile long)array[FLAGS];
    while (prev | indicator) {
        if (CAS(&array[FLAGS], &prev, prev & mask)) {
            return true;
        }
        prev = (volatile long)array[FLAGS];
    }
    return false;
}


static sh_status_e validate_name(
    char const * const name
)   {
    if (name == NULL) {
        return SH_ERR_PATH;
    }
    size_t len = strlen(name);
    if (len == 0) {
        return SH_ERR_PATH;
    }
    if (len > PATH_MAX) {
        return SH_ERR_PATH;
    }
    return SH_OK;
}


static sh_status_e validate_existence(
    char const * const name,
    size_t *size
)   {
    if (size) {
        *size = 0;
    }
    struct stat statbuf;
    int bsize = sizeof(SHR_OBJ_DIR) + strlen(name);
    char nm_buffer[bsize];
    memset(&nm_buffer[0], 0, bsize);
    memcpy(&nm_buffer[0], SHR_OBJ_DIR, sizeof(SHR_OBJ_DIR));
    int index = sizeof(SHR_OBJ_DIR) - 1;
    if (name[0] == '/') {
        index--;
    }
    memcpy(&nm_buffer[index], name, strlen(name));
    int rc = stat(&nm_buffer[0], &statbuf);
    if (rc < 0) {
        if (errno == ENOENT) {
            return SH_ERR_EXIST;
        }
        return convert_to_status(errno);
    }
    if (!S_ISREG(statbuf.st_mode)) {
        return SH_ERR_STATE;
    }
    if (size) {
        *size = statbuf.st_size;
    }
    return SH_OK;
}


static sh_status_e init_array(
    long *array,         // pointer to queue array
    long slots,          // number of allocated slots
    unsigned int max_depth  // max depth allowed at which add of item is blocked
)   {
    array[SIZE] = slots;
    memcpy(&array[TAG], SHRQ, sizeof(SHRQ) - 1);

    int rc = sem_init((sem_t*)&array[READ_SEM], 1, 0);
    if (rc < 0) {
        return SH_ERR_NOSUPPORT;
    }
    rc = sem_init((sem_t*)&array[WRITE_SEM], 1, max_depth);
    if (rc < 0) {
        return SH_ERR_NOSUPPORT;
    }
    // IO_SEM acts as shared memory mutex for resizing mmapped file
    rc = sem_init((sem_t*)&array[IO_SEM], 1, 1);
    if (rc < 0) {
        return SH_ERR_NOSUPPORT;
    }

    array[DATA_ALLOC] = HDR_END + (3 * NODE_SIZE);
    array[FREE_HEAD] = HDR_END;
    array[FREE_HD_CNT] = (long)DELTA * 2;
    array[FREE_TAIL] = HDR_END;
    array[FREE_TL_CNT] = (long)DELTA * 2;
    array[EVENT_HEAD] = HDR_END + NODE_SIZE;
    array[EVENT_HD_CNT] = 0;
    array[EVENT_TAIL] = HDR_END + NODE_SIZE;
    array[EVENT_TL_CNT] = 0;
    array[HEAD] = HDR_END + (2 * NODE_SIZE);
    array[HEAD_CNT] = DELTA;
    array[TAIL] = HDR_END + (2 * NODE_SIZE);
    array[TAIL_CNT] = DELTA;
    array[array[FREE_HEAD]] = HDR_END;
    array[array[FREE_HEAD] + 1] = (long)DELTA * 2;
    array[array[EVENT_HEAD]] = HDR_END + NODE_SIZE;
    array[array[EVENT_HEAD] + 1] = 0;
    array[array[HEAD]] = HDR_END + (2 * NODE_SIZE);
    array[array[HEAD] + 1] = DELTA;

    return SH_OK;
}


static sh_status_e create_named_queue(
    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    unsigned int max_depth, // max depth allowed at which add of item is blocked
    sq_mode_e mode          // read/write mode
)   {
    (*q)->fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, FILE_MODE);
    if ((*q)->fd < 0) {
        free(*q);
        *q = NULL;
        if (errno == EINVAL) {
            return SH_ERR_PATH;
        }
        return convert_to_status(errno);
    }

    int rc = ftruncate((*q)->fd, PAGE_SIZE);
    if (rc < 0) {
        return convert_to_status(errno);
    }

    (*q)->current = calloc(1, sizeof(extent_s));
    if ((*q)->current == NULL) {
        free(*q);
        *q = NULL;
        return SH_ERR_NOMEM;
    }
    (*q)->prev = (*q)->current;

    (*q)->mode = mode;
    (*q)->prot = PROT_READ | PROT_WRITE;
    (*q)->flags = MAP_SHARED;
    (*q)->current->array =
        mmap(0, PAGE_SIZE, (*q)->prot, (*q)->flags, (*q)->fd, 0);
    if ((*q)->current->array == (void*)-1) {
        free((*q)->current);
        free(*q);
        *q = NULL;
        return convert_to_status(errno);
    }

    (*q)->name = strdup(name);
    (*q)->current->size = PAGE_SIZE;
    (*q)->current->slots = (long)PAGE_SIZE >> SZ_SHIFT;

    sh_status_e status = init_array((*q)->current->array, (*q)->current->slots,
        max_depth);
    if (status) {
        free(*q);
        *q = NULL;
    }
    return  status;
}


static void add_end(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    long slot,          // slot reference
    long tail           // tail slot of list
)   {
    DWORD next_after;
    DWORD tail_before;
    DWORD tail_after;
    long next;
    view_s view = {.extent = q->current};
    atomictype * volatile array = (atomictype*)view.extent->array;

    array[slot] = slot;
    next_after.low = slot;

    while(true) {
        tail_before = *((DWORD * volatile)&array[tail]);
        next = tail_before.low;
        array[slot + 1] = tail_before.high + 1;
        next_after.high = tail_before.high + 1;
        view = insure_in_range(q, next, 0);
        array = (atomictype * volatile)view.extent->array;
        if (tail_before.low == array[next]) {
            if (DWCAS((DWORD*)&array[next], &tail_before, next_after)) {
                DWCAS((DWORD*)&array[tail], &tail_before, next_after);
                break;
            }
        } else {
            tail_after = *((DWORD* volatile)&array[next]);
            DWCAS((DWORD*)&array[tail], &tail_before, tail_after);
        }
    }
}


static long calculate_realloc_size(
    extent_s *extent,   // pointer to current extent -- not NULL
    long slots          // number of slots to allocate
)   {
    long current_pages = extent->size >> 12; // divide by page size
    long needed_pages = ((slots << SZ_SHIFT) >> 12) + 1;
    return (current_pages + needed_pages) * PAGE_SIZE;
}

/*
    resize_extent -- resize current mapped extent to match shared memory
    object size

    returns view_s where view.status:

    SH_OK           if view reflects latest state of current queue extent
    SH_ERR_NOMEM    if not enough memory to resize extent to match shared memory
*/
static view_s resize_extent(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    extent_s *extent    // pointer to working extent -- not NULL
)   {
    view_s view = {.status = SH_OK, .slot =  0, .extent = q->current};
    // did another thread change current extent?
    if (extent != view.extent) {
        return view;
    }

    // did another process change size of shared object?
    long *array = view.extent->array;
    if (extent->slots == array[SIZE]) {
        return view;
    }

    // allocate next extent
    extent_s *next = calloc(1, sizeof(extent_s));
    if (next == NULL) {
        view.status = SH_ERR_NOMEM;
        return view;
    }

    next->slots = array[SIZE];
    next->size = next->slots << SZ_SHIFT;

    // extend mapped array
    next->array = mmap(0, next->size, q->prot, q->flags, q->fd, 0);
    if ((char*)next->array == (char *)-1) {
        free(next);
        view.status = SH_ERR_NOMEM;
        view.extent = q->current;
        return view;
    }

    // update current queue extent
    extent_s *tail = view.extent;
    if (CAS((long*)&tail->next, (long*)&null, (long)next)) {
        CAS((long*)&q->current, (long*)&tail, (long)next);
    } else {
        CAS((long*)&q->current, (long*)&tail, (long)tail->next);
        munmap(next->array, next->size);
        free(next);
    }

    view.extent = q->current;
    return view;
}


static view_s expand(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    extent_s *extent,   // pointer to current extent -- not NULL
    long slots          // number of slots to allocate
)   {
    view_s view = {.status = SH_OK, .extent = extent};
    atomictype *array = (atomictype*)extent->array;
    long size = calculate_realloc_size(extent, slots);

    if (extent != q->current) {
        view.extent = q->current;
        return view;
    }

    if (extent->slots != array[SIZE]) {
        return resize_extent(q, extent);
    }

    /*
     *  The only portion of the code that contains a mutex that will potentially
     *  cause other processes to block by locking mutex.
     */

    while (sem_wait((sem_t*)&array[IO_SEM]) < 0) {
        if (errno == EINVAL) {
            view.status = SH_ERR_STATE;
            return view;
        }
    }

    // attempt to extend backing store
    if (extent == q->current && extent->slots == array[SIZE]) {
        while (ftruncate(q->fd, size) < 0) {
            if (errno != EINTR) {
                view.status = SH_ERR_NOMEM;
                break;
            }
        }

        if (view.status == SH_OK) {
            array[SIZE] = size >> SZ_SHIFT;
        }
    }

    // release mutex
    while (sem_post((sem_t*)&array[IO_SEM]) < 0) {
        if (errno == EINVAL) {
            view.status = SH_ERR_STATE;
            return view;
        }
    }

    if (extent->slots != array[SIZE]) {
        view = resize_extent(q, extent);
    }
    return view;
}


static inline view_s insure_in_range(
    shr_q_s *q,         // pointer to queue struct
    long start,         // starting slot
    long slots          // slot count
)   {
    view_s view = {.status = SH_OK, .slot = 0, .extent = q->current};
    if (start < HDR_END) {
        return view;
    }
    if (start + slots >= view.extent->slots) {
        view = resize_extent(q, view.extent);
        if (start + slots >= view.extent->slots) {
            view = expand(q, view.extent, slots);
            if (view.status != SH_OK) {
                return view;
            }
        }
    }
    view.slot = start;
    return view;
}


static long remove_front(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    long ref,           // expected slot number -- 0 if no interest
    long gen,           // generation count
    long head,          // head slot of list
    long tail           // tail slot of list
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    volatile long * volatile array = view.extent->array;
    DWORD before;
    DWORD after;

    if (ref >= HDR_END && ref != array[tail]) {
        view = insure_in_range(q, ref, 0);
        array = view.extent->array;
        after.low = array[ref];
        before.high = gen;
        after.high = before.high + 1;
        before.low = (ulong)ref;
        if (DWCAS((DWORD*)&array[head], &before, after)) {
            memset((void*)&array[ref], 0, 2 << SZ_SHIFT);
            return ref;
        }
    }

    return 0;
}


static view_s alloc_node_slots(
    shr_q_s *q          // pointer to queue struct
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    long *array = view.extent->array;
    long slots = NODE_SIZE;
    long node_alloc;
    long alloc_end;

    while (true) {
        // attempt to remove from free node list
        long gen = array[FREE_HD_CNT];
        node_alloc = array[FREE_HEAD];
        while (node_alloc != array[FREE_TAIL]) {
            node_alloc = remove_front(q, node_alloc, gen, FREE_HEAD, FREE_TAIL);
            if (node_alloc > 0) {
                view = insure_in_range(q, node_alloc, NODE_SIZE);
                array = view.extent->array;
                if (view.slot != 0) {
                    memset(&array[node_alloc], 0, slots << SZ_SHIFT);
                    return view;
                }
            }
            gen = array[FREE_HD_CNT];
            node_alloc = array[FREE_HEAD];
        }

        // attempt to allocate new node from current extent
        node_alloc = array[DATA_ALLOC];
        alloc_end = node_alloc + slots;
        view = insure_in_range(q, node_alloc, NODE_SIZE);
        array = view.extent->array;
        if (alloc_end < array[SIZE]) {
            if (CAS(&array[DATA_ALLOC], &node_alloc, alloc_end)) {
                memset(array+node_alloc, 0, slots << SZ_SHIFT);
                view.slot = node_alloc;
                return view;
            }
            view.extent = q->current;
            array = view.extent->array;
            continue;
        }
    }
    return view;
}


static long find_leaf(
    shr_q_s *q,         // pointer to queue struct
    long count,         // number of slots to return
    long ref_index      // index node reference to begin search
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    long *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
    uint8_t *key = (uint8_t*)&count;
    long node_slot = ref_index;

    while (ref->flag < 0) {
        node_slot = ref->next;
        view = insure_in_range(q, node_slot, 0);
        if (view.slot == 0) {
            return 0;
        }
        array = view.extent->array;
        idx_node_s *node = (idx_node_s*)&array[node_slot];
        long direction = (1 + (ref->bits | key[ref->byte])) >> 8;
        ref = &node->child[direction];
    }
    view = insure_in_range(q, ref->next, 0);
    if (view.slot == 0) {
        return 0;
    }
    return ref->next;
}


static long walk_tree(
    shr_q_s *q,         // pointer to queue struct
    long count,         // number of slots to return
    long *stack,        // stack of node branches to search
    long size,          // depth of stack array
    long top            // top of stack
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    long *array;
    long retries = TSTACK_BRANCHES;

    while (top > 0 && (retries--)) {
        long ref_index = stack[--top];
        view = insure_in_range(q, ref_index, 0);
        if (view.slot != ref_index) {
            return 0;
        }
        array = view.extent->array;
        idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
        if (ref->flag >= 0) {
            view = insure_in_range(q, ref->next, 0);
            if (view.slot != ref->next) {
                return 0;
            }
            array = view.extent->array;
            idx_leaf_s *leaf = (idx_leaf_s*)&array[ref->next];
            if (leaf->count >= count && leaf->allocs != 0) {
                return ref->next;
            }
            continue;
        }
        view = insure_in_range(q, ref->next, 0);
        if (view.slot != ref->next) {
            return 0;
        }
        array = view.extent->array;
        idx_node_s *node = (idx_node_s*)&array[ref->next];
        if (node->child[1].next != 0) {
            if (top >= TSTACK_DEPTH) {
                return 0;
            }
            stack[top] = ref->next + 2;
            top++;
        }
        if (node->child[0].next != 0) {
            if (top >= TSTACK_DEPTH) {
                return 0;
            }
            stack[top] = ref->next;
            top++;
        }
    }
    return 0;
}

static long find_first_fit(
    shr_q_s *q,         // pointer to queue struct
    long count,         // number of slots to return
    long ref_index      // index node reference to begin search
)   {
    long stack[TSTACK_DEPTH] = {0};
    long top = 0;
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    long *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
    uint8_t *key = (uint8_t*)&count;
    long node_slot = ref_index;

    while (ref->flag < 0) {
        node_slot = ref->next;
        view = insure_in_range(q, node_slot, 0);
        if (view.slot == 0) {
            return 0;
        }
        array = view.extent->array;
        idx_node_s *node = (idx_node_s*)&array[node_slot];
        long direction = (1 + (ref->bits | key[ref->byte])) >> 8;
        ref = &node->child[direction];
        // save branch not taken on stack
        if (!direction) {
            if (view.slot == 0) {
                return 0;
            }
            if (node->child[1].next != 0) {
                stack[top++] = node_slot + 2;
            }
        }
    }
    view = insure_in_range(q, ref->next, 0);
    if (view.slot == 0) {
        return 0;
    }
    array = view.extent->array;
    idx_leaf_s *leaf = (idx_leaf_s*)&array[view.slot];
    if (leaf->count >= count && leaf->allocs != 0) {
        return ref->next;
    }
    return walk_tree(q, count, stack, TSTACK_DEPTH, top);
}


static sh_status_e add_to_leaf(
    shr_q_s *q,         // pointer to queue struct
    idx_leaf_s *leaf,
    long slot
)   {
    view_s view = insure_in_range(q, slot, 0);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }

    volatile long * volatile array = view.extent->array;
    DWORD before = {0};
    DWORD after = {0};

    after.low = slot;
    do {
        array[slot + 1] = leaf->allocs_count;
        array[slot] = leaf->allocs;
        before.high = (volatile long)array[slot + 1];
        before.low = (volatile long)array[slot];
        after.high = before.high + 1;
        after.low = slot;
    } while (!DWCAS((DWORD*)&leaf->allocs, &before, after));

    return SH_OK;
}


static sh_status_e add_idx_node(
    shr_q_s *q,         // pointer to queue struct
    long slot,          // start of slot range
    long count,         // number of slots to return
    long ref_index
)   {
    view_s view = alloc_node_slots(q);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }
    long node = view.slot;
    long *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
    idx_leaf_s *leaf = (idx_leaf_s*)&array[node];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;
    array[slot] = 0;
    array[slot + 1] = 0;
    DWORD before = {0, 0};
    DWORD after = { .low = node, .high = 0};

    if (!DWCAS((DWORD*)ref, &before, after)) {
        add_end(q, node, FREE_TAIL);
        return SH_RETRY;
    }
    return SH_OK;
}


static sh_status_e insert_idx_node(
    shr_q_s *q,         // pointer to queue struct
    long slot,          // start of slot range
    long count,         // number of slots to return
    long ref_index,
    long byte,
    uint8_t bits
)   {
    // calculate bit difference
    uint8_t *key = (uint8_t*)&count;
    bits = (uint8_t)~(1 << (31 - __builtin_clz(bits)));
    long newdirection = (1 + (bits | key[byte])) >> 8;

    // create and initialize new leaf
    view_s view = alloc_node_slots(q);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }
    long leaf_index = view.slot;
    long *array = view.extent->array;
    array[slot] = 0;
    array[slot + 1] = 0;
    idx_leaf_s *leaf = (idx_leaf_s*)&array[leaf_index];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;

    // create and initialize new internal node
    view = alloc_node_slots(q);
    if (view.slot == 0) {
        add_end(q, leaf_index, FREE_TAIL);
        return SH_ERR_NOMEM;
    }
    long node_index = view.slot;
    array = view.extent->array;
    idx_node_s *node = (idx_node_s*)&array[node_index];
    idx_ref_s *ref = &node->child[newdirection];
    ref->next = leaf_index;

    // find place to insert new node in tree
    view = insure_in_range(q, ref_index, 0);
    array = view.extent->array;
    idx_ref_s *parent = (idx_ref_s*)&array[ref_index];
    while (true) {
        if (parent->flag >= 0) {
            break;
        }
        if (parent->byte < byte) {
            break;
        }
        if (parent->byte == byte && parent->bits > bits) {
            break;
        }
        long direction = ((1 + (parent->bits | key[parent->byte])) >> 8);
        view = insure_in_range(q, parent->next, 0);
        array = view.extent->array;
        idx_node_s *pnode = (idx_node_s*)&array[parent->next];
        parent = &pnode->child[direction];
    }

    // insert new node in tree
    DWORD before = {0};
    DWORD after = {0};
    ref = &node->child[1 - newdirection];
    ref->next = parent->next;
    ref->diff = parent->diff;
    before.low = ref->next;
    before.high = ref->diff;
    ref = (idx_ref_s*)&after;
    ref->next = node_index;
    ref->byte = byte;
    ref->bits = bits;
    ref->flag = -1;
    if (!DWCAS((DWORD*)parent, &before, after)) {
        add_end(q, leaf_index, FREE_TAIL);
        add_end(q, node_index, FREE_TAIL);
        return SH_RETRY;
    }
    return SH_OK;
}


static sh_status_e free_data_slots(
    shr_q_s *q,         // pointer to queue struct
    long slot           // start of slot range
)   {
    sh_status_e status = SH_RETRY;

    long *array = q->current->array;
    long count = array[slot];

    while (status == SH_RETRY) {
        // check if tree is empty
        if (((idx_ref_s*)&q->current->array[ROOT_FREE])->next == 0) {
            status = add_idx_node(q, slot, count, ROOT_FREE);
            if (status == SH_OK || status != SH_RETRY) {
                break;
            }
            continue;
        }

        long index = find_leaf(q, count, ROOT_FREE);
        if (index == 0) {
            return SH_ERR_NOMEM;
        }
        idx_leaf_s *leaf = (idx_leaf_s*)&q->current->array[index];

        // evaluate differences in keys
        uint8_t bits = 0;
        uint8_t *key = (uint8_t*)&count;
        long byte;
        /*
            different orderings needed for endianness
        */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        for (byte = 0; byte < sizeof(long) ; byte++) {
#else
        for (byte = sizeof(long) - 1; byte >= 0 ; byte--) {
#endif
            bits = key[byte]^leaf->key[byte];
            if (bits) {
                break;
            }
        }
        if (bits == 0) {
            // keys are equal
            return add_to_leaf(q, leaf, slot);
        }

        status = insert_idx_node(q, slot, count, ROOT_FREE, byte, bits);
    }
    return status;
}


static long lookup_freed_data(
    shr_q_s *q,         // pointer to queue struct
    long slots          // number of slots to allocate
)   {
    DWORD before;
    DWORD after;

    if (q == NULL || slots < 2) {
        return 0;
    }
    long index = find_first_fit(q, slots, ROOT_FREE);
    if (index == 0) {
        return 0;
    }
    view_s view = insure_in_range(q, index, 0);
    long *array = view.extent->array;
    idx_leaf_s *leaf = (idx_leaf_s*)&array[index];
    if (slots > leaf->count) {
        return 0;
    }
    do {
        before.low = leaf->allocs;
        before.high = leaf->allocs_count;
        if (before.low == 0) {
            return 0;
        }
        view = insure_in_range(q, before.low, 0);
        array = view.extent->array;
        after.low = (volatile long)array[before.low];
        after.high = before.high + 1;
    } while (before.low != 0 && !DWCAS((DWORD*)&leaf->allocs, &before, after));
    array[before.low] = leaf->count;
    return before.low;
}


static view_s alloc_data_slots(
    shr_q_s *q,         // pointer to queue struct
    long slots          // number of slots to allocate
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    long *array = view.extent->array;
    long node_alloc;
    long alloc_end;

    while (true) {
        if (array[ROOT_FREE] != 0) {
            alloc_end = lookup_freed_data(q, slots);
            if (alloc_end > 0) {
                view = insure_in_range(q, alloc_end, slots);
                array = view.extent->array;
                if (view.slot != 0) {
                    memset(&array[alloc_end + 1], 0, (slots - 1) << SZ_SHIFT);
                    return view;
                }
            }
        }

        long space = slots;
        if (__builtin_popcountl(space) > 1) {
            int bit_len = sizeof(space) * 8;
            int pos = __builtin_clzl(space);
            int shifts = bit_len - pos;
            space = 1 << shifts;
        }
        node_alloc = array[DATA_ALLOC];
        alloc_end = node_alloc + space;
        // assert(alloc_end - node_alloc == space);
        view = insure_in_range(q, node_alloc, space);
        array = view.extent->array;
        if (alloc_end < array[SIZE]) {
            if (CAS(&array[DATA_ALLOC], &node_alloc, alloc_end)) {
                memset(array+node_alloc, 0, space << SZ_SHIFT);
                view.slot = node_alloc;
                array[node_alloc] = space;
                return view;
            }
            view.extent = q->current;
            array = view.extent->array;
            continue;
        }
    }
    return view;
}


static inline long calc_data_slots(
    long length
) {
    long space = DATA_HDR;
    // calculate number of slots needed for data
    space += (length >> SZ_SHIFT);
    // account for remainder
    if (length & REM) {
        space += 1;
    }

    return space;
}

static inline void update_buffer_size(
    long *array,
    long space,
    long vcnt
)   {
    long total = space << SZ_SHIFT;
    total += vcnt * sizeof(sq_vec_s);
    long buff_sz = array[BUFFER];
    while (total > buff_sz) {
        if (CAS(&array[BUFFER], &buff_sz, total)) {
            break;
        }
        buff_sz = array[BUFFER];
    }
}

static long copy_value(
    shr_q_s *q,         // pointer to queue struct
    void *value,        // pointer to value data
    long length,        // length of data
    sh_type_e type      // data type
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return 0;
    }

    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    long space = calc_data_slots(length);

    update_buffer_size(q->current->array, space, 1);

    view_s view = alloc_data_slots(q, space);
    long current = view.slot;
    if (current >= HDR_END) {
        long *array = view.extent->array;
        array[current + TM_SEC] = curr_time.tv_sec;
        array[current + TM_NSEC] = curr_time.tv_nsec;
        array[current + TYPE] = type;
        array[current + VEC_CNT] = 1;
        array[current + DATA_LENGTH] = length;
        memcpy(&array[current + DATA_HDR], value, length);
    }

    return current;
}


static long calc_vector_slots(
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 2
) {
    long space = DATA_HDR;

    for(int i = 0; i < vcnt; i++) {
        // increment for embedded for type and data length
        space += 2;
        // calculate number of slots needed for data
        space += (vector[i].len >> SZ_SHIFT);
        // account for remainder
        if (vector[i].len & REM) {
            space += 2;
        }
    }

    return space;
}


static long copy_vector(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 2
)   {
    if (q == NULL || vector == NULL || vcnt < 2) {
        return 0;
    }

    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    long space = calc_vector_slots(vector, vcnt);

    update_buffer_size(q->current->array, space, vcnt);

    view_s view = alloc_data_slots(q, space);
    long current = view.slot;
    if (current >= HDR_END) {
        long *array = view.extent->array;
        array[current + TM_SEC] = curr_time.tv_sec;
        array[current + TM_NSEC] = curr_time.tv_nsec;
        array[current + TYPE] = SH_VECTOR_T;
        array[current + VEC_CNT] = vcnt;
        array[current + DATA_LENGTH] = (space - DATA_HDR) << SZ_SHIFT;
        long slot = current;
        slot += DATA_HDR;
        for (int i = 0; i < vcnt; i++) {
            if (vector[i].type <= 0 || vector[i].len <= 0 || vector[i].base == NULL) {
                return -1;
            }
            array[slot++] = vector[i].type;
            array[slot++] = vector[i].len;
            memcpy(&array[slot], vector[i].base, vector[i].len);
            slot += (vector[i].len >> SZ_SHIFT);
            if (vector[i].len & REM) {
                slot++;
            }
        }
    }

    return current;
}


static void release_prev_extents(
    shr_q_s *q          // pointer to queue -- not NULL
)   {
    extent_s *head = q->prev;
    extent_s *next;

    while (head != q->current) {
        if (q->accessors > 1) {
            break;
        }
        next = head->next;
        if (!CAS((long*)&q->prev, (long*)&head, (long)next)) {
            break;
        }
        munmap(head->array, head->size);
        free(head);
        head = next;
    }
}


static void signal_arrival(
    shr_q_s *q
)   {
    if (q->current->array[LISTEN_SIGNAL] == 0 ||
        q->current->array[LISTEN_PID] == 0) {
        return;
    }
    int sval = -1;
    (void)sem_getvalue((sem_t*)&q->current->array[READ_SEM], &sval);
    union sigval sv = {.sival_int = sval};

    if (sval == 0) {
        (void)sigqueue(q->current->array[LISTEN_PID], q->current->array[LISTEN_SIGNAL], sv);
    }
}


static void signal_event(
    shr_q_s *q
)   {
    if (q->current->array[NOTIFY_PID] == 0 ||
        q->current->array[NOTIFY_SIGNAL] == 0) {
        return;
    }
    union sigval sv = {0};

    (void)sigqueue(q->current->array[NOTIFY_PID], q->current->array[NOTIFY_SIGNAL], sv);
}


static void signal_call(
    shr_q_s *q
)   {
    if (q->current->array[CALL_PID] == 0 ||
        q->current->array[CALL_SIGNAL] == 0) {
        return;
    }
    union sigval sv = {0};

    (void)sigqueue(q->current->array[CALL_PID], q->current->array[CALL_SIGNAL], sv);
}


static inline bool is_monitored(
    long *array
)   {
    return (array[NOTIFY_SIGNAL] && array[NOTIFY_PID]);
}


static inline bool is_call_monitored(
    long *array
)   {
    return (array[CALL_SIGNAL] && array[CALL_PID]);
}


static inline bool is_discard_on_expire(
    long *array
)   {
    return (array[FLAGS] & FLAG_DISCARD_EXPIRED);
}


static inline bool is_adaptive_lifo(
    long *array
)   {
    return (array[FLAGS] & FLAG_LIFO_ON_LEVEL);
}


static inline bool is_codel_active(
    long *array
)   {
    return ((array[TARGET_NSEC] || array[TARGET_SEC]) &&
        (array[LIMIT_NSEC] || array[LIMIT_NSEC]));
}


static long get_event_flag(
    sq_event_e event
)   {
    switch (event) {
    case SQ_EVNT_ALL:
        return (FLAG_EVNT_INIT | FLAG_EVNT_LIMIT | FLAG_EVNT_EMPTY |
            FLAG_EVNT_LEVEL | FLAG_EVNT_NONEMPTY | FLAG_EVNT_TIME);
    case SQ_EVNT_INIT:
        return FLAG_EVNT_INIT;
    case SQ_EVNT_LIMIT:
        return FLAG_EVNT_LIMIT;
    case SQ_EVNT_EMPTY:
        return FLAG_EVNT_EMPTY;
    case SQ_EVNT_LEVEL:
        return FLAG_EVNT_LEVEL;
    case SQ_EVNT_NONEMPTY:
        return FLAG_EVNT_NONEMPTY;
    case SQ_EVNT_TIME:
        return FLAG_EVNT_TIME;
    default:
        break;
    }
    return 0;
}


static inline bool event_disabled(
    long *array,
    sq_event_e event
)   {
    return !(array[FLAGS] & get_event_flag(event));
}


static bool add_event(
    shr_q_s *q,
    sq_event_e event
)   {
    view_s view = {.extent = q->current};

    long *array = view.extent->array;

    if (event == SQ_EVNT_NONE || event_disabled(array, event)) {
        return false;
    }

    if (array[NOTIFY_SIGNAL] == 0 || array[NOTIFY_PID] == 0) {
        return false;
    }

    // allocate queue node
    view = alloc_node_slots(q);
    if (view.slot == 0) {
        return false;
    }
    array = view.extent->array;

    array[view.slot + EVENT_OFFSET] = event;

    // append node to end of queue
    add_end(q, view.slot, EVENT_TAIL);

    return true;
}


static void notify_event(
    shr_q_s *q,
    sq_event_e event
)   {
    if(is_monitored(q->current->array)) {
        if (add_event(q, event)) {
            signal_event(q);
        }
    }
}


static void update_empty_timestamp(
    long *array      // active q array
)   {
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    struct timespec last = *(struct timespec * volatile)&array[EMPTY_SEC];
    DWORD next = {.low = curr_time.tv_sec, .high = curr_time.tv_nsec};
    while (timespeccmp(&curr_time, &last, >)) {
        if (DWCAS((DWORD*)&array[EMPTY_SEC], (DWORD*)&last, next)) {
            break;
        }
        last = *(struct timespec * volatile)&array[EMPTY_SEC];
    }
}


static void stack_push(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    long slot           // slot reference
)   {
    DWORD stack_before;
    DWORD stack_after;
    view_s view = insure_in_range(q, slot, 0);
    atomictype * volatile array = (atomictype*)view.extent->array;

    do {
        array[slot] = array[STACK_HEAD];
        array[slot + 1] = array[STACK_HD_CNT];
        stack_before = *((DWORD * volatile)&array[slot]);
        stack_after.low = slot;
        stack_after.high = stack_before.high + 1;
    } while (!DWCAS((DWORD*)&array[STACK_HEAD], &stack_before, stack_after));
}


static sh_status_e enq_data(
    shr_q_s *q,         // pointer to queue, not NULL
    long data_slot      // data to be added to queue
)   {
    if (q == NULL || data_slot < HDR_END) {
        return SH_ERR_ARG;
    }
    long node;
    extent_s *extent = q->current;
    long *array = extent->array;

    DWORD curr_time;
    curr_time.low = array[data_slot + TM_SEC];
    curr_time.high = array[data_slot + TM_NSEC];

    // allocate queue node
    view_s view = alloc_node_slots(q);
    if (view.slot == 0) {
        free_data_slots(q, data_slot);
        return SH_ERR_NOMEM;
    }
    node = view.slot;
    array = view.extent->array;

    // point queue node to data slot
    array[node + VALUE_OFFSET] = data_slot;

    long count = array[COUNT];
    if (is_adaptive_lifo(array) && (count + 1 > array[LEVEL])) {
        stack_push(q, node);
    } else {
        // append node to end of queue
        add_end(q, node, TAIL);
    }

    count = AFA(&array[COUNT], 1);

    if (is_monitored(array)) {
        if (count == 0) {
            // queue emptied
            update_empty_timestamp(array);
        }
        bool need_signal = false;
        if (!(array[FLAGS] & FLAG_ACTIVATED)) {
            if (set_flag(array, FLAG_ACTIVATED)) {
                need_signal |= add_event(q, SQ_EVNT_INIT);
            }
        }
        if (count == 0) {
            need_signal |= add_event(q, SQ_EVNT_NONEMPTY);
        }
        if (need_signal) {
            signal_event(q);
        }
    }
    signal_arrival(q);

    // update last add timestamp
    struct timespec prev_time = *(struct timespec*)&array[TS_SEC];
    DWCAS((DWORD*)&array[TS_SEC], (DWORD*)&prev_time, curr_time);

    release_prev_extents(q);

    return SH_OK;
}


static sh_status_e enq(
    shr_q_s *q,         // pointer to queue, not NULL
    void *value,        // pointer to item, not NULL
    size_t length,      // length of item
    sh_type_e type      // data type
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return SH_ERR_ARG;
    }
    long data_slot;

    // allocate space and copy value
    data_slot = copy_value(q, value, length, type);
    if (data_slot == 0) {
        return SH_ERR_NOMEM;
    }
    if (data_slot < HDR_END) {
        return SH_ERR_STATE;
    }
    return enq_data(q, data_slot);
}


static sh_status_e enqv(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 2
)   {
    if (q == NULL || vector == NULL || vcnt < 2) {
        return 0;
    }
    long data_slot;

    // allocate space and copy vector
    data_slot = copy_vector(q, vector, vcnt);
    if (data_slot < 0) {
        return SH_ERR_ARG;
    }
    if (data_slot == 0) {
        return SH_ERR_NOMEM;
    }
    if (data_slot < HDR_END) {
        return SH_ERR_STATE;
    }
    return enq_data(q, data_slot);
}


static long next_item(
    shr_q_s *q,          // pointer to queue
    long slot
)   {
    view_s view = insure_in_range(q, slot, 0);
    if (view.slot == 0) {
        return 0;
    }
    long *array = view.extent->array;
    long next = array[slot];
    if (next == 0) {
        return 0;
    }
    view = insure_in_range(q, next, 0);
    if (view.slot == 0) {
        return 0;
    }
    array = view.extent->array;
    if (next && next < LONG_MAX - EVENT_TAIL) {
        return array[next + VALUE_OFFSET];
    }
    return 0;
}


static bool item_exceeds_limit(
    shr_q_s *q,                     // pointer to queue
    long item_slot,                 // array index for item
    struct timespec *timelimit,     // expiration timelimit
    struct timespec *curr_time      // current time
)   {
    if (q == NULL || item_slot < HDR_END || timelimit == NULL) {
        return false;
    }
    if (timelimit->tv_sec == 0 && timelimit->tv_nsec == 0) {
        return false;
    }
    view_s view = insure_in_range(q, item_slot, 0);
    long *array = view.extent->array;
    struct timespec diff = {0, 0};
    struct timespec *item = (struct timespec *)&array[item_slot + TM_SEC];
    timespecsub(curr_time, item, &diff);
    if (timespeccmp(&diff, timelimit, >)) {
        return true;
    }
    return false;
}

static bool item_exceeds_delay(
    shr_q_s *q,                     // pointer to queue
    long item_slot,                 // array index for item
    long *array                     // array to access
)   {
    struct timespec current;
    clock_gettime(CLOCK_REALTIME, &current);
    if (is_codel_active(array)) {
        struct timespec intrvl = {0};
        struct timespec last = *(struct timespec*)&array[EMPTY_SEC];
        if (last.tv_sec == 0) {
            return item_exceeds_limit(q, item_slot,
                (struct timespec*)&array[LIMIT_SEC], &current);
        }
        timespecsub(&current, (struct timespec*)&array[LIMIT_SEC], &intrvl);
        if (timespeccmp(&last, &intrvl, <)) {
            return item_exceeds_limit(q, item_slot,
                (struct timespec*)&array[TARGET_SEC], &current);
        }
    }
    return item_exceeds_limit(q, item_slot, (struct timespec*)&array[LIMIT_SEC],
        &current);
}


static void clear_empty_timestamp(
    long *array      // active q array
)   {
    volatile struct timespec last =
        *(struct timespec * volatile)&array[EMPTY_SEC];
    DWORD next = {.high = 0, .low = 0};
    while (!DWCAS((DWORD*)&array[EMPTY_SEC], (DWORD*)&last, next)) {
        last = *(struct timespec * volatile)&array[EMPTY_SEC];
    }
}


static void copy_to_buffer(
    long *array,        // pointer to queue array
    long data_slot,     // data item index
    sq_item_s *item,    // pointer to item
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size   // pointer to length of buffer if buffer present
)   {
    long size = (array[data_slot + DATA_SLOTS] << SZ_SHIFT) - sizeof(long);
    long total = size + array[data_slot + VEC_CNT] * sizeof(sq_vec_s);
    if (*buffer && *buff_size < total) {
        free(*buffer);
        *buffer = NULL;
        *buff_size = 0;
    }
    if (*buffer == NULL) {
        *buffer = malloc(total);
        *buff_size = total;
        if (*buffer == NULL) {
            item->status = SH_ERR_NOMEM;
            return;
        }
    }

    memcpy(*buffer, &array[data_slot + 1], size);
    item->buffer = *buffer;
    item->buf_size = size;
    item->type = array[data_slot + TYPE];
    item->length = array[data_slot + DATA_LENGTH];
    item->timestamp = *buffer;
    item->value = (uint8_t*)*buffer + ((DATA_HDR - 1) * sizeof(long));
    item->vcount = array[data_slot + VEC_CNT];
    item->vector = (sq_vec_s*)((uint8_t*)*buffer + size);
    if (item->vcount == 1) {
        item->vector[0].type = (sh_type_e)array[data_slot + TYPE];
        item->vector[0].len = item->length;
        item->vector[0].base = item->value;
    } else {
        uint8_t *current = (uint8_t*)item->value;
        for (int i = 0; i < item->vcount; i++) {
            item->vector[i].type = (sh_type_e)*(long*)current;
            current += sizeof(long);
            item->vector[i].len = *(long*)current;
            current += sizeof(long);
            item->vector[i].base = current;
            if (item->vector[i].len <= sizeof(long)) {
                current += sizeof(long);
            } else {
                current += (item->vector[i].len >> SZ_SHIFT) << SZ_SHIFT;
                if (item->vector[i].len & REM) {
                    current += sizeof(long);
                }
            }
        }
    }
}


static long remove_top(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    long top,           // expected slot number -- 0 if no interest
    long gen            // generation count
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    volatile long * volatile array = view.extent->array;
    DWORD before;
    DWORD after;

    if (top >= HDR_END && top == array[STACK_HEAD] &&
        gen == array[STACK_HD_CNT]) {
        view = insure_in_range(q, top, 0);
        array = view.extent->array;
        after.low = array[top];
        before.high = gen;
        after.high = before.high + 1;
        before.low = (ulong)top;
        if (DWCAS((DWORD*)&array[STACK_HEAD], &before, after)) {
            memset((void*)&array[top], 0, 2 << SZ_SHIFT);
            return top;
        }
    }

    return 0;
}

static long stack_pop(
    shr_q_s *q          // pointer to queue
)   {
    long *array = q->current->array;
    long gen = array[STACK_HD_CNT];
    long top = array[STACK_HEAD];
    view_s view = insure_in_range(q, top, 0);
    array = view.extent->array;
    long data_slot = array[top + VALUE_OFFSET];
    if (data_slot == 0) {
        return data_slot;   // try again
    }
    if (remove_top(q, top, gen) == 0) {
        return 0;
    }
    // free queue node
    add_end(q, top, FREE_TAIL);
    return data_slot;
}


static long queue_remove(
    shr_q_s *q          // pointer to queue
)   {
    long *array = q->current->array;
    long gen = array[HEAD_CNT];
    long head = array[HEAD];
    if (head == array[TAIL]) {
        return 0;
    }
    view_s view = insure_in_range(q, head, 0);
    array = view.extent->array;
    long data_slot = next_item(q, head);
    if (data_slot == 0) {
        return data_slot;   // try again
    }
    if (remove_front(q, head, gen, HEAD, TAIL) == 0) {
        return 0;
    }
    // free queue node
    add_end(q, head, FREE_TAIL);
    return data_slot;
}


static sq_item_s deq(
    shr_q_s *q,         // pointer to queue
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size   // pointer to length of buffer if buffer present
)   {
    view_s view = {.extent = q->current};
    long *array = view.extent->array;
    sq_item_s item = {.status = SH_ERR_EMPTY};
    long data_slot;

    while (true) {
        if (array[STACK_HEAD] != 0) {
            data_slot = stack_pop(q);
        } else {
            long head = array[HEAD];
            if (head == array[TAIL]) {
                break;
            }
            data_slot = queue_remove(q);
        }
        if (data_slot == 0) {
            continue;
        }
        // insure data is accessible
        view = insure_in_range(q, data_slot, 0);
        if (view.slot == 0) {
            break;
        }
        array = view.extent->array;
        long end_slot = data_slot + array[data_slot + DATA_SLOTS] - 1;
        view = insure_in_range(q, end_slot, 0);
        if (view.slot == 0) {
            break;
        }
        array = view.extent->array;

        copy_to_buffer(array, data_slot, &item, buffer, buff_size);

        long count = AFS(&array[COUNT], 1);

        if (is_codel_active(array) && count == 1) {
            clear_empty_timestamp(array);
        }

        bool expired = (is_discard_on_expire(array) || is_monitored(array) ?
            item_exceeds_delay(q, data_slot, array) : false);
        if (is_monitored(array)) {
            bool need_signal = false;
            if (count == 1) {
                need_signal |= add_event(q, SQ_EVNT_EMPTY);
            }
            if (expired) {
                need_signal |= add_event(q, SQ_EVNT_TIME);
            }
            if (need_signal) {
                signal_event(q);
            }
        }

        if (expired && is_discard_on_expire(array)) {
            memset(&item, 0, sizeof(item));
            free_data_slots(q, data_slot);
            item.status = SH_ERR_EXIST;
            break;
        }

        free_data_slots(q, data_slot);
        item.status = SH_OK;
        break;
    }

    release_prev_extents(q);

    return item;
}


static sq_event_e next_event(
    shr_q_s *q,          // pointer to queue
    long slot
)   {
    view_s view = insure_in_range(q, slot, 0);
    if (view.slot == 0) {
        return SQ_EVNT_NONE;
    }
    long *array = view.extent->array;

    long next = array[slot];
    view = insure_in_range(q, next, 0);
    if (view.slot == 0) {
        return SQ_EVNT_NONE;
    }
    array = view.extent->array;
    return array[next + EVENT_OFFSET];
}


static void check_level(
    shr_q_s *q          // pointer to queue
)   {
    view_s view = {.extent = q->current};
    long *array = view.extent->array;

    if (array[NOTIFY_SIGNAL] == 0 || array[NOTIFY_PID] == 0) {
        return;
    }

    long level = array[LEVEL];
    if (level <= 0) {
        return;
    }

    int sval = 0;
    if (sem_getvalue((sem_t*)&q->current->array[READ_SEM], &sval) < 0) {
        return;
    }
    if (sval < level) {
        return;
    }

    if (!CAS(&array[LEVEL], &level, 0)) {
        return;
    }

    // allocate queue node
    view = alloc_node_slots(q);
    if (view.slot == 0) {
        return;
    }
    array = view.extent->array;
    long node = view.slot;

    array[node + EVENT_OFFSET] = SQ_EVNT_LEVEL;

    // append node to end of queue
    add_end(q, node, EVENT_TAIL);

    signal_event(q);

    return;
}


/*==============================================================================

    public function interface

==============================================================================*/


/*
    shr_q_create -- create shared memory queue using name

    Creates shared queue using name to mmap POSIX shared memory object.

    The max depth argument specifies the maximum number of items allowed on
    queue.  When max depth is reached a depth event is generated and no more
    items can be added to queue.  A value of 0 defaults to max possible value.

    The mode specifies the ability to add items to or remove items from the
    queue.  The default of 0 indicates that queue instance is unable to make
    changes to the shared queue.

    The returned queue will be opened for updates unless the mode specifies it
    as immutable.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct is NULL, max_depth is less than
                    zero, or if no queue name
    SH_ERR_ACCESS   on permissions error for queue name
    SH_ERR_EXIST    if queue already exists
    SH_ERR_PATH     if error in queue name
    SH_ERR_SYS      if system call returns an error
*/
extern sh_status_e shr_q_create(
    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    unsigned int max_depth, // max depth allowed at which add of item is blocked
    sq_mode_e mode          // read/write mode
)   {
    if (q == NULL ||
        name == NULL ||
        max_depth > SEM_VALUE_MAX) {
        return SH_ERR_ARG;
    }

    sh_status_e status = validate_name(name);
    if (status) {
        return status;
    }

    status = validate_existence(name, NULL);
    if (status == SH_OK) {
        return SH_ERR_EXIST;
    }
    if (status != SH_ERR_EXIST) {
        return status;
    }

    if (max_depth == 0) {
        max_depth = SEM_VALUE_MAX;
    }

    *q = calloc(1, sizeof(shr_q_s));
    if (*q == NULL) {
        return SH_ERR_NOMEM;
    }

    return create_named_queue(q, name, max_depth, mode);
}

/*
    shr_q_open -- open shared memory queue for modification using name

    Opens shared queue using name to mmap shared memory.  Queue must already
    exist before it is opened.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct or name is not NULL
    SH_ERR_ACCESS   on permissions error for queue name
    SH_ERR_EXIST    if queue does not already exist
    SH_ERR_PATH     if error in queue name
    SH_ERR_STATE    if incompatible implementation version number
    SH_ERR_SYS      if system call returns an error
*/
extern sh_status_e shr_q_open(
    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    sq_mode_e mode          // read/write mode
)   {
    if (q == NULL || name == NULL) {
        return SH_ERR_ARG;
    }

    sh_status_e status = validate_name(name);
    if (status) {
        return status;
    }

    size_t size = 0;
    status = validate_existence(name, &size);
    if (status) {
        return status;
    }

    if ((size < PAGE_SIZE) || (size % PAGE_SIZE != 0)){
        return SH_ERR_STATE;
    }

    *q = calloc(1, sizeof(shr_q_s));
    if (*q == NULL) {
        return SH_ERR_NOMEM;
    }

    (*q)->fd = shm_open(name, O_RDWR, FILE_MODE);
    if ((*q)->fd < 0) {
        free(*q);
        *q = NULL;
        return convert_to_status(errno);
    }

    (*q)->mode = mode;

    (*q)->current = calloc(1, sizeof(extent_s));
    if ((*q)->current == NULL) {
        free(*q);
        *q = NULL;
        return SH_ERR_NOMEM;
    }
    (*q)->prev = (*q)->current;

    int prot = PROT_READ | PROT_WRITE;
    (*q)->flags = MAP_SHARED;
    do {
        (*q)->current->array = mmap(0, size, prot, (*q)->flags, (*q)->fd, 0);
        if ((*q)->current->array == (void*)-1) {
            free((*q)->current);
            free(*q);
            *q = NULL;
            return convert_to_status(errno);
        }
        if (size == (*q)->current->array[SIZE] << SZ_SHIFT) {
            break;
        }
        size_t alt_size = (*q)->current->array[SIZE] << SZ_SHIFT;
        munmap((*q)->current->array, size);
        size = alt_size;
    } while (true);
    (*q)->prot = prot;
    (*q)->current->size = size;
    (*q)->current->slots = size >> SZ_SHIFT;

    (*q)->name = strdup(name);

    if (memcmp(&(*q)->current->array[TAG], SHRQ, sizeof(SHRQ) - 1) != 0) {
        shr_q_close(q);
        return SH_ERR_STATE;
    }
    if ((*q)->current->array[VERSION] != 0) {
        shr_q_close(q);
        return SH_ERR_STATE;
    }

    return SH_OK;
}

/*
    shr_q_close -- close shared memory queue

    Closes shared queue and releases associated memory.  The pointer to the
    queue instance will be NULL on return.

    returns sh_status_e:

    SH_OK       on success
    SH_ERR_ARG  if pointer to queue struct is NULL
*/
extern sh_status_e shr_q_close(
    shr_q_s **q         // address of q struct pointer -- not NULL
)   {
    if (q == NULL || *q == NULL) {
        return SH_ERR_ARG;
    }

    release_prev_extents(*q);

    if ((*q)->current) {
        if ((*q)->current->array) {
            munmap((*q)->current->array, (*q)->current->size);
        }
        free((*q)->current);
    }

    if ((*q)->fd > 0) {
        close((*q)->fd);
    }

    if ((*q)->name){
        free((*q)->name);
    }

    free(*q);
    *q = NULL;

    return SH_OK;
}


/*
    shr_q_destroy -- unlink and release shared memory queue

    Unlink shared queue and releases associated memory and resources.  The
    pointer to the queue instance will be NULL on return. Shared queue will not
    be available or usable to other processes.

    returns sh_status_e:

    SH_OK       on success
    SH_ERR_ARG  if pointer to queue struct is NULL
    SH_ERR_SYS  if an error occurs releasing associated resources
*/
extern sh_status_e shr_q_destroy(
    shr_q_s **q         // address of q struct pointer -- not NULL
)   {
    sh_status_e status = SH_OK;

    if (q == NULL || *q == NULL) {
        return SH_ERR_ARG;
    }

    int rc = sem_destroy((sem_t*)&(*q)->current->array[READ_SEM]);
    if (rc < 0) {
        status = SH_ERR_SYS;
    }
    rc = sem_destroy((sem_t*)&(*q)->current->array[WRITE_SEM]);
    if (rc < 0) {
        status = SH_ERR_SYS;
    }
    rc = sem_destroy((sem_t*)&(*q)->current->array[IO_SEM]);
    if (rc < 0) {
        status = SH_ERR_SYS;
    }

    release_prev_extents(*q);

    if ((*q)->current) {
        if ((*q)->current->array) {
            munmap((*q)->current->array, (*q)->current->size);
        }
        free((*q)->current);
    }

    if ((*q)->fd > 0) {
        close((*q)->fd);
    }

    if ((*q)->name){
        int rc = shm_unlink((*q)->name);
        if (rc < 0) {
            status = SH_ERR_SYS;
        }
        free((*q)->name);
    }

    free(*q);
    *q = NULL;

    return status;
}


/*
    shr_q_monitor -- registers calling process for notification when events
                     occur on shared queue

    Any non-zero value registers calling process for notification  using the
    specified signal when an queue event occurs.  A value of zero unregisters
    the process if it is currently registered.  Only a single process can be
    registered for event notifications.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct is NULL, or if signal not greater
                    than or equal to zero, or signal not in valid range
    SH_ERR_STATE    if unable to add pid, or unregistering and pid does not match
*/
extern sh_status_e shr_q_monitor(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for event notification
)   {
    if (q == NULL || signal < 0) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);

    long pid = getpid();
    if (signal == 0) {
        if (pid != q->current->array[NOTIFY_PID]) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }

        if (CAS(&q->current->array[NOTIFY_PID], &pid, 0)) {
            q->current->array[NOTIFY_SIGNAL] = signal;
            (void)AFS(&q->accessors, 1);
            return SH_OK;
        }
        (void)AFS(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    long prev = q->current->array[NOTIFY_PID];
    if (CAS(&q->current->array[NOTIFY_PID], &prev, pid)) {
        q->current->array[NOTIFY_SIGNAL] = signal;
        (void)AFS(&q->accessors, 1);
        return SH_OK;
    }

    (void)AFS(&q->accessors, 1);
    return SH_ERR_STATE;
}


/*
    shr_q_listen -- registers calling process for notification when items
                    arrive on shared queue

    Any non-zero value registers calling process for notification  using the
    specified signal when an item arrives on queue.  A value of zero unregisters
    the process if it is currently registered.  Only a single process can be
    registered for event notifications.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct is NULL, or if signal not greater
                    than or equal to zero, or signal not in valid range
    SH_ERR_STATE    if unable to add pid, or unregistering and pid does not match
*/
extern sh_status_e shr_q_listen(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for item arrival notification
)   {
    if (q == NULL || signal < 0) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);

    long pid = getpid();
    if (signal == 0) {
        if (pid != q->current->array[LISTEN_PID]) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }

        if (CAS(&q->current->array[LISTEN_PID], &pid, 0)) {
            q->current->array[LISTEN_SIGNAL] = signal;
            (void)AFS(&q->accessors, 1);
            return SH_OK;
        }
        (void)AFS(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    long prev = q->current->array[LISTEN_PID];
    if (CAS(&q->current->array[LISTEN_PID], &prev, pid)) {
        q->current->array[LISTEN_SIGNAL] = signal;
        (void)AFS(&q->accessors, 1);
        return SH_OK;
    }

    (void)AFS(&q->accessors, 1);
    return SH_ERR_STATE;
}


/*
    shr_q_call -- registers calling process for notification when queue removes
                  will block because queue is empty

    Any non-zero value registers calling process for notification  using the
    specified signal when a call blocks on remove from queue.  A value of zero
    unregisters the process if it is currently registered.  Only a single
    process can be registered for event notifications.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct is NULL, or if signal not greater
                    than or equal to zero, or signal not in valid range
    SH_ERR_STATE    if unable to add pid, or unregistering and pid does not match
*/
extern sh_status_e shr_q_call(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for queue empty notification
)   {
    if (q == NULL || signal < 0) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);

    long pid = getpid();
    if (signal == 0) {
        if (pid != q->current->array[CALL_PID]) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }

        if (CAS(&q->current->array[CALL_PID], &pid, 0)) {
            q->current->array[NOTIFY_SIGNAL] = signal;
            (void)AFS(&q->accessors, 1);
            return SH_OK;
        }
        (void)AFS(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    long prev = q->current->array[CALL_PID];
    if (CAS(&q->current->array[CALL_PID], &prev, pid)) {
        q->current->array[CALL_SIGNAL] = signal;
        (void)AFS(&q->accessors, 1);
        return SH_OK;
    }

    (void)AFS(&q->accessors, 1);
    return SH_ERR_STATE;
}


/*
    shr_q_add -- add item to queue

    Non-blocking add of an item to shared queue.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_LIMIT    if queue size is at maximum depth
    SH_ERR_ARG      if q is NULL, value is NULL, or length is <= 0
    SH_ERR_STATE    if q is immutable or read only or q corrupted
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_add(
    shr_q_s *q,         // pointer to queue -- not NULL
    void *value,        // pointer to item -- not NULL
    size_t length       // length of item -- greater than 0
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    while (sem_trywait((sem_t*)&array[WRITE_SEM]) < 0) {
        if (errno == EAGAIN) {
            notify_event(q, SQ_EVNT_LIMIT);
            (void)AFS(&q->accessors, 1);
            return SH_ERR_LIMIT;
        }
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status = enq(q, value, length, SH_STRM_T);

    if (status != SH_OK) {
        sem_post((sem_t*)&array[WRITE_SEM]);
        (void)AFS(&q->accessors, 1);
        return status;
    }

    array = q->current->array;
    while (sem_post((sem_t*)&array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            status = SH_ERR_STATE;
            (void)AFS(&q->accessors, 1);
            return status;
        }
    }

    if (is_monitored(array)) {
        check_level(q);
    }

    (void)AFS(&q->accessors, 1);
    return status;
}


/*
    shr_q_add_wait -- attempt to add item to queue

    Attempt add of an item to shared queue, and block if at max depth limit
    until depth limit allows.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL, value is NULL, or length is <= 0
    SH_ERR_STATE    if q is immutable or read only or q corrupted
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_add_wait(
    shr_q_s *q,         // pointer to queue -- not NULL
    void *value,        // pointer to item -- not NULL
    size_t length       // length of item -- greater than 0
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    int sval = 0;
    if (sem_getvalue((sem_t*)&array[WRITE_SEM], &sval) == 0 &&
        sval == 0) {
        notify_event(q, SQ_EVNT_LIMIT);
    }

    while (sem_wait((sem_t*)&array[WRITE_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status = enq(q, value, length, SH_STRM_T);

    if (status != SH_OK) {
        sem_post((sem_t*)&array[WRITE_SEM]);
        (void)AFS(&q->accessors, 1);
        return status;
    }

    array = q->current->array;
    while (sem_post((sem_t*)&array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    if (is_monitored(array)) {
        check_level(q);
    }

    (void)AFS(&q->accessors, 1);
    return status;
}


/*
    shr_q_add_timedwait -- attempt to add item to queue

    Attempt add of an item to shared queue, and block if at max depth limit
    until depth limit allows or timeout value reached.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_LIMIT    if queue size is at maximum depth
    SH_ERR_ARG      if q is NULL, value is NULL, length is <= 0, or timeout
                    is NULL
    SH_ERR_STATE    if q is immutable or read only or q corrupted
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_add_timedwait(
    shr_q_s *q,                 // pointer to queue -- not NULL
    void *value,                // pointer to item -- not NULL
    size_t length,              // length of item -- greater than 0
    struct timespec *timeout    // timeout value -- not NULL
)   {
    if (q == NULL || value == NULL || length <= 0 || timeout == NULL) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    int sval = 0;
    if (sem_getvalue((sem_t*)&array[WRITE_SEM], &sval) == 0 &&
        sval == 0) {
        notify_event(q, SQ_EVNT_LIMIT);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    timespecadd(&ts, timeout, &ts);
    while (sem_timedwait((sem_t*)&array[WRITE_SEM], &ts) < 0) {
        if (errno == ETIMEDOUT) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_LIMIT;
        }
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status = enq(q, value, length, SH_STRM_T);

    if (status != SH_OK) {
        sem_post((sem_t*)&array[WRITE_SEM]);
        (void)AFS(&q->accessors, 1);
        return status;
    }

    array = q->current->array;
    while (sem_post((sem_t*)&array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    if (is_monitored(array)) {
        check_level(q);
    }

    (void)AFS(&q->accessors, 1);
    return status;
}


/*
    shr_q_addv -- add vector of items to queue

    Non-blocking add of a vector of items to shared queue.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_LIMIT    if queue size is at maximum depth
    SH_ERR_ARG      if q is NULL, vector is NULL, or vcnt is < 1
    SH_ERR_STATE    if q is immutable or read only
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_addv(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 1
)   {
    if (q == NULL || vector == NULL || vcnt < 1) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    while (sem_trywait((sem_t*)&array[WRITE_SEM]) < 0) {
        if (errno == EAGAIN) {
            notify_event(q, SQ_EVNT_LIMIT);
            (void)AFS(&q->accessors, 1);
            return SH_ERR_LIMIT;
        }
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status;
    if (vcnt == 1) {
        status = enq(q, vector[0].base, vector[0].len, vector[0].type);
    } else {
        status = enqv(q, vector, vcnt);
    }

    if (status != SH_OK) {
        sem_post((sem_t*)&array[WRITE_SEM]);
        (void)AFS(&q->accessors, 1);
        return status;
    }

    array = q->current->array;
    while (sem_post((sem_t*)&array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            status = SH_ERR_STATE;
            (void)AFS(&q->accessors, 1);
            return status;
        }
    }

    if (is_monitored(array)) {
        check_level(q);
    }

    (void)AFS(&q->accessors, 1);
    return status;
}


/*
    shr_q_addv_wait -- attempt to add vector of items to queue

    Attempt to add a vector of items to shared queue, and block if at max depth
    limit until depth limit allows.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL, vector is NULL, or vcnt is < 1
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_addv_wait(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 1
)   {
    if (q == NULL || vector == NULL || vcnt < 1) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    int sval = 0;
    if (sem_getvalue((sem_t*)&array[WRITE_SEM], &sval) == 0 &&
        sval == 0) {
        notify_event(q, SQ_EVNT_LIMIT);
    }

    while (sem_wait((sem_t*)&array[WRITE_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status;
    if (vcnt == 1) {
        status = enq(q, vector[0].base, vector[0].len, vector[0].type);
    } else {
        status = enqv(q, vector, vcnt);
    }

    if (status != SH_OK) {
        sem_post((sem_t*)&array[WRITE_SEM]);
        (void)AFS(&q->accessors, 1);
        return status;
    }

    array = q->current->array;
    while (sem_post((sem_t*)&array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    if (is_monitored(array)) {
        check_level(q);
    }

    (void)AFS(&q->accessors, 1);
    return status;
}


/*
    shr_q_addv_timedwait -- attempt to add vector of items to queue for
                            specified period

    Attempt to add a vector of items to shared queue, and block if at max depth
    limit until depth limit allows or timeout value reached.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_LIMIT    if queue size is at maximum depth
    SH_ERR_ARG      if q is NULL, vector is NULL, vcnt is < 1, or timeout
                    is NULL
    SH_ERR_STATE    if q is immutable or read only or q corrupted
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_addv_timedwait(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_vec_s *vector,           // pointer to vector of items -- not NULL
    int vcnt,                   // count of vector array -- must be >= 1
    struct timespec *timeout    // timeout value -- not NULL
)   {
    if (q == NULL || vector == NULL || vcnt < 1 || timeout == NULL) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    int sval = 0;
    if (sem_getvalue((sem_t*)&array[WRITE_SEM], &sval) == 0 &&
        sval == 0) {
        notify_event(q, SQ_EVNT_LIMIT);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    timespecadd(&ts, timeout, &ts);
    while (sem_timedwait((sem_t*)&array[WRITE_SEM], &ts) < 0) {
        if (errno == ETIMEDOUT) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_LIMIT;
        }
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status;
    if (vcnt == 1) {
        status = enq(q, vector[0].base, vector[0].len, vector[0].type);
    } else {
        status = enqv(q, vector, vcnt);
    }

    if (status != SH_OK) {
        sem_post((sem_t*)&array[WRITE_SEM]);
        (void)AFS(&q->accessors, 1);
        return status;
    }

    array = q->current->array;
    while (sem_post((sem_t*)&array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    if (is_monitored(array)) {
        check_level(q);
    }

    (void)AFS(&q->accessors, 1);
    return status;
}


/*
    shr_q_remove -- remove item from queue

    Non-blocking remove of an item from shared queue.  If buffer is provided, it
    will be validated as being large enough using the specified size, and will
    contain the data upon return.  If buffer is not large enough, the memory
    will be released using free and allocated with malloc to be the correct size
    to hold the queued item data.  Upon resizing of buffer, the length will be
    large enough to contain the returned data.  If buffer is not provided, a
    buffer will be allocated that is at least large enough to contain the item
    being removed and the length of buffer will be updated.

    A struct of type sq_item_s is returned in all cases, with the status of the
    call, a pointer to the data value being returned in the buffer, the length
    of the data being returned, and a pointer to timestamp in the buffer of when
    the item was added to the queue.

    returned sh_status_e:

    SH_OK           on success
    SH_ERR_EMPTY    if q is empty
    SH_ERR_ARG      if q is NULL, or buffer pointer not NULL and length <= 0
    SH_ERR_STATE    if q is immutable or write only
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sq_item_s shr_q_remove(
    shr_q_s *q,         // pointer to queue structure -- not NULL
    void **buffer,      // address of buffer pointer -- not NULL
    size_t *buff_size   // pointer to size of buffer -- not NULL
)   {
    if (q == NULL ||
        buffer == NULL ||
        buff_size == NULL ||
        (*buffer != NULL && *buff_size <= 0)) {

        return (sq_item_s){.status = SH_ERR_ARG};
    }

    if (!(q->mode & SQ_READ_ONLY)) {
        return (sq_item_s){.status = SH_ERR_STATE};
    }

    sq_item_s item = {0};

    (void)AFA(&q->accessors, 1);

    long *array;

    while (true) {
        array = q->current->array;
        while (sem_trywait((sem_t*)&array[READ_SEM]) < 0) {
            if (errno == EAGAIN) {
                if (is_call_monitored(array)) {
                    signal_call(q);
                }
                (void)AFS(&q->accessors, 1);
                item.status = SH_ERR_EMPTY;
                return item;
            }
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                item.status = SH_ERR_STATE;
                return item;
            }
        }

        item = deq(q, buffer, buff_size);

        if (item.status != SH_OK && item.status != SH_ERR_EXIST) {
            sem_post((sem_t*)&q->current->array[READ_SEM]);
            break;
        }

        while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                return (sq_item_s){.status = SH_ERR_STATE};
            }
        }

        if (item.status == SH_OK) {
            break;
        }
    }

    (void)AFS(&q->accessors, 1);
    return item;
}


/*
    shr_q_remove_wait -- attempt to remove item from queue, block if empty

    Remove an item from shared queue and block if queue is empty.  If
    buffer is provided, it will be validated as being large enough using
    the specified size, and will contain the data upon return.  If buffer is not
    large enough, the memory will be released using free and allocated with
    malloc to be the correct size to hold the queued item data.  Upon resizing
    of buffer, the length will large enough to contain the returned data.  If
    buffer is not provided, a buffer will be allocated that is large enough to
    contain the data being removed and the length of buffer will be updated.

    A struct of type sq_item_s is returned in all cases, with the status of the
    call, a pointer to the data value being returned in the buffer, the length
    of the data being returned, and a pointer to timestamp in the buffer of when
    the item was added to the queue.

    returned sh_status_e:

    SH_OK           on success
    SH_ERR_EMPTY    if q is empty
    SH_ERR_ARG      if q is NULL, or buffer pointer not NULL and length <= 0
    SH_ERR_STATE    if q is immutable or write only
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sq_item_s shr_q_remove_wait(
    shr_q_s *q,             // pointer to queue struct -- not NULL
    void **buffer,          // address of buffer pointer -- not NULL
    size_t *buff_size       // pointer to size of buffer -- not NULL
)   {
    if (q == NULL ||
        buffer == NULL ||
        buff_size == NULL ||
        (*buffer != NULL && *buff_size <= 0)) {

        return (sq_item_s){.status = SH_ERR_ARG};
    }

    if (!(q->mode & SQ_READ_ONLY)) {
        return (sq_item_s){.status = SH_ERR_STATE};
    }

    sq_item_s item = {0};

    (void)AFA(&q->accessors, 1);

    while (true) {
        (void)AFA(&q->current->array[CALL_BLOCKS], 1);
        if (is_call_monitored(q->current->array)) {
            signal_call(q);
        }
        while (sem_wait((sem_t*)&q->current->array[READ_SEM]) < 0) {
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                item.status = SH_ERR_STATE;
                return item;
            }
        }
        (void)AFA(&q->current->array[CALL_UNBLOCKS], 1);

        item = deq(q, buffer, buff_size);

        if (item.status != SH_OK && item.status != SH_ERR_EXIST) {
            sem_post((sem_t*)&q->current->array[READ_SEM]);
            break;
        }

        while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                return (sq_item_s){.status = SH_ERR_STATE};
            }
        }

        if (item.status == SH_OK) {
            break;
        }
    }

    (void)AFS(&q->accessors, 1);
    return item;
}


/*
    shr_q_remove_timedwait -- attempt to remove item from queue for specified
    amount of time

    Remove an item from shared queue that blocks if queue is empty, but only
    for time period specfied by timeout value.  If buffer is provided, it will
    be validated as being large enough using the specified size, and will
    contain the data upon return.  If buffer is not large enough, the memory
    will be released using free and allocated with malloc to be the correct size
    to hold the queued item data.  Upon resizing of buffer, the length will
    large enough to contain the returned data.  If buffer is not provided, a
    buffer will be allocated that is large enough to contain the data being
    removed and the length of buffer will be updated.

    A struct of type sq_item_s is returned in all cases, with the status of the
    call, a pointer to the data value being returned in the buffer, the length
    of the data being returned, and a pointer to timestamp in the buffer of when
    the item was added to the queue.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_EMPTY    if q is empty
    SH_ERR_ARG      if q is NULL, or buffer pointer not NULL and length <= 0
    SH_ERR_STATE    if q is immutable or write only
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sq_item_s shr_q_remove_timedwait(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size,          // pointer to size of buffer -- not NULL
    struct timespec *timeout    // timeout value -- not NULL
)   {
    if (q == NULL ||
        buffer == NULL ||
        buff_size == NULL ||
        (*buffer != NULL && *buff_size <= 0) ||
        timeout == NULL) {
        return (sq_item_s){.status = SH_ERR_ARG};
    }

    if (!(q->mode & SQ_READ_ONLY)) {
        return (sq_item_s){.status = SH_ERR_STATE};
    }

    sq_item_s item = {0};

    (void)AFA(&q->accessors, 1);

    while (true) {
        (void)AFA(&q->current->array[CALL_BLOCKS], 1);
        if (is_call_monitored(q->current->array)) {
            signal_call(q);
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        timespecadd(&ts, timeout, &ts);
        while (sem_timedwait((sem_t*)&q->current->array[READ_SEM], &ts) < 0) {
            if (errno == ETIMEDOUT) {
                (void)AFA(&q->current->array[CALL_UNBLOCKS], 1);
                (void)AFS(&q->accessors, 1);
                item.status = SH_ERR_EMPTY;
                return item;
            }
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                item.status = SH_ERR_STATE;
                return item;
            }
        }
        (void)AFA(&q->current->array[CALL_UNBLOCKS], 1);

        item = deq(q, buffer, buff_size);

        if (item.status != SH_OK && item.status != SH_ERR_EXIST) {
            sem_post((sem_t*)&q->current->array[READ_SEM]);
            break;
        }

        while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                return (sq_item_s){.status = SH_ERR_STATE};
            }
        }

        if (item.status == SH_OK) {
            break;
        }
    }

    (void)AFS(&q->accessors, 1);
    return item;
}


/*
    shr_q_event -- returns active event or SQ_EVNT_NONE when empty

*/
extern sq_event_e shr_q_event(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return SQ_EVNT_NONE;
    }
    (void)AFA(&q->accessors, 1);

    extent_s *extent = q->current;
    long *array = extent->array;
    sq_event_e event = SQ_EVNT_NONE;

    long gen = array[EVENT_HD_CNT];
    long head = array[EVENT_HEAD];
    while (head != array[EVENT_TAIL]) {
        event = next_event(q, head);
        if (remove_front(q, head, gen, EVENT_HEAD, EVENT_TAIL) != 0) {
            // free queue node
            add_end(q, head, FREE_TAIL);
            break;
        }
        gen = array[EVENT_HD_CNT];
        head = array[EVENT_HEAD];
    }

    release_prev_extents(q);

    (void)AFS(&q->accessors, 1);
    return event;
}


/*
    shr_q_explain -- return a null-terminated string explanation of status code

    returns non-NULL null-terminated string error explanation
*/
extern char *shr_q_explain(
    sh_status_e status          // status code
)   {
    if (status > SH_ERR_SYS) {
        return status_str[SH_ERR_SYS + 1];
    }
    return status_str[status];
}


/*
    shr_q_exceeds_idle_time -- tests to see if no item has been added within the
    specified time limit

    returns true if limit is equaled or exceeded, otherwise false
*/
extern bool shr_q_exceeds_idle_time(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t lim_secs,            // time limit in seconds
    long lim_nsecs              // time limie in nanoseconds
)   {
    if (q == NULL) {
        return false;
    }
    (void)AFA(&q->accessors, 1);

    extent_s *extent = q->current;
    long *array = extent->array;
    struct timespec curr_time;
    (void)clock_gettime(CLOCK_REALTIME, &curr_time);

    if (curr_time.tv_sec - array[TS_SEC] > lim_secs) {
        (void)AFS(&q->accessors, 1);
        return true;
    }
    if (curr_time.tv_sec - array[TS_SEC] < lim_secs) {
        (void)AFS(&q->accessors, 1);
        return false;
    }
    if (curr_time.tv_nsec - array[TS_NSEC] > lim_nsecs) {
        (void)AFS(&q->accessors, 1);
        return true;
    }
    if (curr_time.tv_nsec - array[TS_NSEC] < lim_nsecs) {
        (void)AFS(&q->accessors, 1);
        return false;
    }

    (void)AFS(&q->accessors, 1);
    return true;
}


/*
    shr_q_count -- returns count of items on queue, or -1 if it fails

*/
extern long shr_q_count(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return -1;
    }
    (void)AFA(&q->accessors, 1);
    long result = -1;
    result = q->current->array[COUNT];
    (void)AFS(&q->accessors, 1);
    return result;
}


/*
    shr_q_buffer -- returns max size needed to read items from queue

*/
extern size_t shr_q_buffer(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return 0;
    }
    (void)AFA(&q->accessors, 1);
    long result = -1;
    result = q->current->array[BUFFER];
    (void)AFS(&q->accessors, 1);
    return result;
}


/*
    shr_q_level -- sets value for queue depth level event generation and for
        adaptive LIFO

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL, or level not greater than 0

*/
extern sh_status_e shr_q_level(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    int level                   // level at which to generate level event
)   {
    if (q == NULL || level <= 0) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);

    extent_s *extent = q->current;
    long *array = extent->array;
    long prev = array[LEVEL];
    CAS(&array[LEVEL], &prev, level);

    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_timelimit -- sets time limit of item on queue before producing a max
    time limit event

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL

*/
extern sh_status_e shr_q_timelimit(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t seconds,             // number of seconds till event
    long nanoseconds            // number of nanoseconds till event
)   {
    if (q == NULL) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    struct timespec prev;
    DWORD next;
    next.low = seconds;
    next.high = nanoseconds;
    do {
        prev.tv_sec = (volatile time_t)array[LIMIT_SEC];
        prev.tv_nsec = (volatile long)array[LIMIT_NSEC];
    } while (!DWCAS((DWORD*)&array[LIMIT_SEC], (DWORD*)&prev, next));
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_clean  -- remove items from front of queue that have exceeded
    specified time limit

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q or timespec is NULL
    SH_ERR_STATE    if q is immutable or write only, or not a valid queue
    SH_ERR_NOMEM    if not enough memory to satisfy request

*/
extern sh_status_e shr_q_clean(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    struct timespec *timelimit  // timelimit value -- not NULL
)   {
    if (q == NULL || timelimit == NULL) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_READ_ONLY)) {
        return SH_ERR_STATE;
    }

    struct timespec curr_time;

    (void)AFA(&q->accessors, 1);

    while(true) {
        while (sem_trywait((sem_t*)&q->current->array[READ_SEM]) < 0) {
            if (errno == EAGAIN) {
                (void)AFS(&q->accessors, 1);
                return SH_OK;
            }
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                return SH_ERR_STATE;
            }
        }

        long *array = q->current->array;
        long gen = array[HEAD_CNT];
        long head = array[HEAD];
        if (head == array[TAIL]) {
            break;
        }
        view_s view = insure_in_range(q, head, 0);
        array = view.extent->array;
        long data_slot = next_item(q, head);
        if (data_slot == 0) {
            break;
        }
        // insure data is accessible
        view = insure_in_range(q, data_slot, 0);
        if (view.slot == 0) {
            break;
        }

        clock_gettime(CLOCK_REALTIME, &curr_time);
        if (!item_exceeds_limit(q, data_slot, timelimit, &curr_time)) {
            break;
        }

        if (remove_front(q, head, gen, HEAD, TAIL) == 0) {
            break;
        }

        AFS(&array[COUNT], 1);

        // free queue node
        add_end(q, head, FREE_TAIL);
        free_data_slots(q, data_slot);

        while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
            if (errno == EINVAL) {
                (void)AFS(&q->accessors, 1);
                return SH_ERR_STATE;
            }
        }
    }

    while (sem_post((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
shr_q_last_empty  -- returns timestamp of last time queue became non-empty

    Note:  Only updates if there is a registered monitoring process

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q or timestamp is NULL
    SH_ERR_EMPTY    if q is currently empty, timestamp not updated

*/
extern sh_status_e shr_q_last_empty(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    struct timespec *timestamp  // timestamp pointer -- not NULL
)   {
    if (q == NULL || timestamp == NULL) {
        return SH_ERR_ARG;
    }

    (void)AFA(&q->accessors, 1);
    if (q->current->array[COUNT] == 0) {
        (void)AFS(&q->accessors, 1);
        return SH_ERR_EMPTY;
    }
    *timestamp = *(struct timespec *)&q->current->array[EMPTY_SEC];
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_discard  -- discard items that exceed expiration time limit

    Note:  default on creation is to NOT discard, even if time limit is set

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL

*/
extern sh_status_e shr_q_discard(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    bool flag                   // true will cause items to be discarded
)   {
    if (q == NULL) {
        return SH_ERR_ARG;
    }

    (void)AFA(&q->accessors, 1);

    if (flag) {
        set_flag(q->current->array, FLAG_DISCARD_EXPIRED);
    } else {
        clear_flag(q->current->array, FLAG_DISCARD_EXPIRED);
    }
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_will_discard -- tests to see if queue will discard expired items

    returns true if expired items will be discarded, otherwise false
*/
extern bool shr_q_will_discard(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return false;
    }

    (void)AFA(&q->accessors, 1);
    bool result = is_discard_on_expire(q->current->array);
    (void)AFS(&q->accessors, 1);
    return result;
}


/*
    shr_q_limit_lifo  -- treat depth limit as limit for adaptive LIFO behavior

    Once depth limit is reached, items will be processed in LIFO rather
    than FIFO ordering.  If depth limit is set to 0, or otherwise defaults to 0,
    all items on queue will be processed in LIFO rather than FIFO order.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL

*/
extern sh_status_e shr_q_limit_lifo(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    bool flag                   // true will turn on adaptive LIFO behavior
)   {
    if (q == NULL) {
        return SH_ERR_ARG;
    }

    (void)AFA(&q->accessors, 1);

    if (flag) {
        set_flag(q->current->array, FLAG_LIFO_ON_LEVEL);
    } else {
        clear_flag(q->current->array, FLAG_LIFO_ON_LEVEL);
    }
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_will_lifo -- tests to see if queue will used adaptive LIFO

    returns true if queue will use adaptive LIFO, otherwise false
*/
extern bool shr_q_will_lifo(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return false;
    }

    (void)AFA(&q->accessors, 1);
    bool result = is_adaptive_lifo(q->current->array);
    (void)AFS(&q->accessors, 1);
    return result;
}


/*
    shr_q_subscribe  -- enable previously disabled event

    Note:  default on creation is that if there is a monitoring process then
    all events are disabled and there must be subscription to see them

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL

*/
extern sh_status_e shr_q_subscribe(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_event_e event            // event to enable
)   {
    if (q == NULL) {
        return SH_ERR_ARG;
    }
    long flag = get_event_flag(event);
    (void)AFA(&q->accessors, 1);
    if (flag) {
        set_flag(q->current->array, flag);
    }
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_unsubscribe  -- disable previously enabled event

    Note:  default on creation is that if there is a monitoring process then
    all events are disabled and there must be subscription to see them

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL

*/
extern sh_status_e shr_q_unsubscribe(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_event_e event            // event to disable
)   {
    if (q == NULL) {
        return SH_ERR_ARG;
    }
    long flag = get_event_flag(event);
    (void)AFA(&q->accessors, 1);
    if (flag) {
        clear_flag(q->current->array, flag);
    }
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_is_subscribed -- tests a single event to see if it will be generated

    returns true if event has subscription, otherwise false
*/
extern bool shr_q_is_subscribed(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_event_e event            // event to disable
)   {
    if (q == NULL || event == SQ_EVNT_NONE || event == SQ_EVNT_ALL) {
        return false;
    }

    (void)AFA(&q->accessors, 1);
    bool result = !event_disabled(q->current->array, event);
    (void)AFS(&q->accessors, 1);
    return result;
}


/*
    shr_q_prod -- activates at least one blocked caller if there are blocked
                  remove calls

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL
    SH_ERR_STATE    if q is immutable or read only or q corrupted
*/
extern sh_status_e shr_q_prod(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);
    while (sem_post((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }
    (void)AFS(&q->accessors, 1);
    return SH_OK;
}


/*
    shr_q_call_count -- returns count of blocked remove calls, or -1 if it fails

*/
extern long shr_q_call_count(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return -1;
    }
    long unblocks = q->current->array[CALL_UNBLOCKS];
    return q->current->array[CALL_BLOCKS] - unblocks;
}


/*
    shr_q_target_delay -- sets target delay and activates CoDel algorithm

    Note: will automatically set discard items on expiration

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL

*/
extern sh_status_e shr_q_target_delay(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t seconds,             // delay number of seconds
    long nanoseconds            // delay number of nanoseconds
) {
    if (q == NULL) {
        return SH_ERR_ARG;
    }
    (void)AFA(&q->accessors, 1);

    long *array = q->current->array;
    struct timespec prev;
    DWORD next;
    next.low = seconds;
    next.high = nanoseconds;
    do {
        prev.tv_sec = (volatile time_t)array[TARGET_SEC];
        prev.tv_nsec = (volatile long)array[TARGET_NSEC];
    } while (!DWCAS((DWORD*)&array[TARGET_SEC], (DWORD*)&prev, next));
    (void)AFS(&q->accessors, 1);
    return shr_q_discard(q, true);
}


/*
    shr_q_is_valid -- returns true if name is a valid queue

*/
extern bool shr_q_is_valid(
    char const * const name // name of q as a null terminated string -- not NULL
)   {
    sh_status_e status = validate_name(name);
    if (status) {
        return false;
    }

    size_t size = 0;
    status = validate_existence(name, &size);
    if (status) {
        return false;
    }

    if (size < PAGE_SIZE) {
        return false;
    }

    int fd = shm_open(name, O_RDONLY, FILE_MODE);
    if (fd < 0) {
        return false;
    }

    long *array = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
    if (array == (void*)-1) {
        close(fd);
        return false;
    }

    if (memcmp(&array[TAG], SHRQ, sizeof(SHRQ) - 1) != 0) {
        munmap(array, size);
        close(fd);
        return false;
    }

    if (array[VERSION] != 0) {
        munmap(array, size);
        close(fd);
        return false;
    }

    munmap(array, size);
    close(fd);
    return true;
}


/*==============================================================================

    unit tests

==============================================================================*/


#ifdef TESTMAIN

long adds;
long events;

static void sig_usr(int signo)
{
    if (signo == SIGUSR1) {
        adds++;
    } else if (signo == SIGUSR2) {
        events++;
    }
}


static void set_signal_handlers(void)
{
    if (signal(SIGUSR1, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGUSR1\n");
    }
    if (signal(SIGUSR2, sig_usr) == SIG_ERR) {
        printf("cannot catch SIGUSR1\n");
    }
}


static void test_CAS(void)
{
    long original = 1;
    long prev = 1;
    long next = 2;
    assert(CAS(&original, &prev, next));
    assert(prev == 1);
    assert(original == 2);
    assert(!CAS(&original, &prev, next));
}


static void test_DWCAS(void)
{
    DWORD original = {.low = 1, .high = 2};
    DWORD prev = {.low = 1, .high = 2};
    DWORD next = {.low = 3, .high = 4};
    assert(DWCAS(&original, &prev, next));
    assert(prev.low == 1);
    assert(prev.high == 2);
    assert(original.low == 3);
    assert(original.high == 4);
    assert(!DWCAS(&original, &prev, next));
}


static void test_create_error_paths(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", UINT_MAX, SQ_IMMUTABLE);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, "testq", -1, SQ_IMMUTABLE);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, NULL, 1, SQ_READ_ONLY);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, NULL, 1, SQ_WRITE_ONLY);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, NULL, 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_ARG);
    assert(q == NULL);
    status = shr_q_create(&q, "/fake/testq", 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_PATH);
    assert(q == NULL);
    status = shr_q_create(&q, "fake/testq", 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_PATH);
    assert(q == NULL);
    int fd = shm_open("/test",  O_RDWR | O_CREAT, FILE_MODE);
    assert(fd > 0);
    status = shr_q_create(&q, "/test", 1, SQ_IMMUTABLE);
    assert(status == SH_ERR_EXIST);
    assert(q == NULL);
    assert(shm_unlink("/test") == 0);
}

static void test_create_namedq(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *pq = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_destroy(NULL);
    assert(status == SH_ERR_ARG);
    assert(q != NULL);
    status = shr_q_destroy(&pq);
    assert(status == SH_ERR_ARG);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_monitor(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_monitor(q, -1);
    assert(status == SH_ERR_ARG);
    status = shr_q_monitor(q, 0);
    assert(status == SH_ERR_STATE);
    status = shr_q_monitor(q, SIGURG);
    assert(status == SH_OK);
    status = shr_q_monitor(q, SIGUSR1);
    assert(status == SH_OK);
    status = shr_q_monitor(q, 0);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_listen(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_listen(q, -1);
    assert(status == SH_ERR_ARG);
    status = shr_q_listen(q, 0);
    assert(status == SH_ERR_STATE);
    status = shr_q_listen(q, SIGURG);
    assert(status == SH_OK);
    status = shr_q_listen(q, SIGUSR1);
    assert(status == SH_OK);
    status = shr_q_listen(q, 0);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_call(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_call(q, -1);
    assert(status == SH_ERR_ARG);
    status = shr_q_call(q, 0);
    assert(status == SH_ERR_STATE);
    status = shr_q_call(q, SIGURG);
    assert(status == SH_OK);
    status = shr_q_call(q, SIGUSR1);
    assert(status == SH_OK);
    status = shr_q_call(q, 0);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_open_close(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    // close error conditions
    status = shr_q_close(NULL);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&q);
    assert(status == SH_ERR_ARG);

    // open error conditions
    status = shr_q_open(NULL, "testq", SQ_READWRITE);
    assert(status == SH_ERR_ARG);
    status = shr_q_open(&q, NULL, SQ_READWRITE);
    assert(status == SH_ERR_ARG);
    status = shr_q_open(&q, "badq", SQ_READWRITE);
    assert(status == SH_ERR_EXIST);

    // successful open and close
    status = shr_q_open(&q, "testq", SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    status = shr_q_open(&q, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    status = shr_q_open(&q, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_alloc_node_slots(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    status = shr_q_create(&q, "/testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    view_s view = alloc_node_slots(q);
    assert(view.slot > 0);
    add_end(q, view.slot, FREE_TAIL);
    long first = view.slot;
    view = alloc_node_slots(q);
    assert(view.slot > 0);
    add_end(q, view.slot, FREE_TAIL);
    view = alloc_node_slots(q);
    assert(view.slot > 0);
    assert(view.slot == first);
    add_end(q, view.slot, FREE_TAIL);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_free_data_array4(long *array)
{
    sh_status_e status;
    long slot[4];
    shr_q_s *q = NULL;
    shm_unlink("testq");
    status = shr_q_create(&q, "/testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    view_s view = alloc_data_slots(q, array[0]);
    slot[0] = view.slot;
    assert(slot[0] > 0);
    view = alloc_data_slots(q, array[1]);
    slot[1] = view.slot;
    assert(slot[1] > 0);
    view = alloc_data_slots(q, array[2]);
    slot[2] = view.slot;
    assert(slot[2] > 0);
    view = alloc_data_slots(q, array[3]);
    slot[3] = view.slot;
    assert(slot[3] > 0);
    status = free_data_slots(q, slot[0]);
    assert(status == SH_OK);
    status = free_data_slots(q, slot[1]);
    assert(status == SH_OK);
    status = free_data_slots(q, slot[2]);
    assert(status == SH_OK);
    status = free_data_slots(q, slot[3]);
    assert(status == SH_OK);
    view = alloc_data_slots(q, array[0]);
    assert(view.slot == slot[0]);
    view = alloc_data_slots(q, array[1]);
    assert(view.slot == slot[1]);
    view = alloc_data_slots(q, array[2]);
    assert(view.slot == slot[2]);
    view = alloc_data_slots(q, array[3]);
    assert(view.slot == slot[3]);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_free_data_slots(void)
{
    long test1[4] = {8, 16, 32, 64};
    test_free_data_array4(test1);
    long test2[4] = {64, 32, 16, 8};
    test_free_data_array4(test2);
    long test3[4] = {64, 16, 8, 32};
    test_free_data_array4(test3);
    long test4[4] = {64, 8, 32, 16};
    test_free_data_array4(test4);
    long test5[4] = {8, 64, 16, 32};
    test_free_data_array4(test5);
}

static void test_first_fit_allocation(void)
{
    sh_status_e status;
    long biggest_slot = 0;
    long bigger_slot = 0;
    view_s view;
    shr_q_s *q = NULL;
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 64);
    biggest_slot = view.slot;
    assert(biggest_slot > 0);
    view = alloc_data_slots(q, 32);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    status = free_data_slots(q, biggest_slot);
    assert(status == SH_OK);
    status = free_data_slots(q, bigger_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 20);
    assert(view.slot == bigger_slot);
    view = alloc_data_slots(q, 20);
    assert(view.slot == biggest_slot);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 64);
    biggest_slot = view.slot;
    assert(biggest_slot > 0);
    view = alloc_data_slots(q, 32);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    view = alloc_data_slots(q, 16);
    assert(view.slot != 0);
    status = free_data_slots(q, view.slot);
    assert(status == SH_OK);
    status = free_data_slots(q, biggest_slot);
    assert(status == SH_OK);
    status = free_data_slots(q, bigger_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 20);
    assert(view.slot == bigger_slot);
    assert(view.extent->array[view.slot] == 32);
    view = alloc_data_slots(q, 20);
    assert(view.slot == biggest_slot);
    assert(view.extent->array[view.slot] == 64);
    view = alloc_data_slots(q, 20);
    assert(view.extent->array[view.slot] == 32);
    view = alloc_data_slots(q, 20);
    assert(view.extent->array[view.slot] == 32);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_large_data_allocation(void)
{
    sh_status_e status;
    long big_slot = 0;
    long bigger_slot = 0;
    long slot = 0;
    shr_q_s *q = NULL;
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    view_s view = alloc_data_slots(q, 4096 >> SZ_SHIFT);
    big_slot = view.slot;
    assert(big_slot > 0);
    status = free_data_slots(q, big_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 8192 >> SZ_SHIFT);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    status = free_data_slots(q, bigger_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 4096 >> SZ_SHIFT);
    slot = view.slot;
    assert(slot > 0);
    assert(big_slot == slot);
    status = free_data_slots(q, big_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 8192 >> SZ_SHIFT);
    slot = view.slot;
    assert(slot > 0);
    assert(bigger_slot == slot);
    view = alloc_data_slots(q, 4096 >> SZ_SHIFT);
    slot = view.slot;
    assert(slot > 0);
    assert(big_slot == slot);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_add(void)
{
    sh_status_e status;
    sq_item_s item;
    long length = 4;
    size_t size = length;
    char *msg = calloc(1, length);
    void *buffer = msg;
    shr_q_s *q = NULL;
    status = shr_q_open(&q, "testq", SQ_READWRITE);
    assert(status == SH_OK);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_ERR_LIMIT);
    item = shr_q_remove(q, &buffer, &size);
    assert(item.status == SH_OK);
    assert(buffer != msg);
    assert(item.length == 4);
    assert(memcmp(item.value, "test", item.length) == 0);
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    item = shr_q_remove(q, &buffer, &size);
    assert(item.status == SH_OK);
    assert(item.length == 5);
    assert(memcmp(item.value, "test1", item.length) == 0);
    status = shr_q_close(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(buffer);
}

static void test_add_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;

    status = shr_q_add(q, "test", 4);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_add(tq, NULL, 4);
    assert(status == SH_ERR_ARG);
    status = shr_q_add(tq, "test", 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_add_wait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;

    status = shr_q_add_wait(q, "test", 4);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_add_wait(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_wait(q, "test", 4);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_wait(tq, NULL, 4);
    assert(status == SH_ERR_ARG);
    status = shr_q_add_wait(tq, "test", 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_add_timedwait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    struct timespec ts = {0};

    status = shr_q_add_timedwait(q, "test", 4, &ts);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_add_timedwait(q, "test", 4, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_timedwait(q, "test", 4, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_add_timedwait(tq, NULL, 4, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_add_timedwait(tq, "test", 0, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_add_timedwait(tq, "test", 4, NULL);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_remove_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_item_s item;

    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove(tq, NULL, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove(q, &item.buffer, NULL);
    assert(item.status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}

static void test_remove_wait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_item_s item;

    item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_wait(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_wait(tq, NULL, &item.buf_size);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove_wait(q, &item.buffer, NULL);
    assert(item.status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}

static void test_remove_timedwait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_item_s item;
    struct timespec ts = {0};

    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &ts);
    assert(item.status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &ts);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &ts);
    assert(item.status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    item = shr_q_remove_timedwait(tq, NULL, &item.buf_size, &ts);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove_timedwait(q, &item.buffer, NULL, &ts);
    assert(item.status == SH_ERR_ARG);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, NULL);
    assert(item.status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}


static void test_is_valid(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shm_unlink("testq");
    assert(!shr_q_is_valid(NULL));
    assert(!shr_q_is_valid(""));
    assert(!shr_q_is_valid("testq"));
    int fd = shm_open("testq", O_RDWR | O_CREAT | O_EXCL, FILE_MODE);
    assert(fd > 0);
    int rc = ftruncate(fd, PAGE_SIZE >> 1);
    assert(rc == 0);
    assert(!shr_q_is_valid("testq"));
    rc = ftruncate(fd, PAGE_SIZE);
    assert(rc == 0);
    assert(!shr_q_is_valid("testq"));
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    assert(shr_q_is_valid("testq"));
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_addv_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_vec_s vector[2] = {{0, 0}, {0, 0}};

    status = shr_q_addv(q, vector, 1);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_addv(q, NULL, 1);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv(q, vector, 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv(q, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = NULL;
    vector[0].len = 4;
    status = shr_q_addv(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    vector[0].len = 4;
    vector[1].base = "test1";
    vector[1].len = 0;
    status = shr_q_addv(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    vector[1].base = NULL;
    vector[1].len = 5;
    status = shr_q_addv(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_addv_wait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    sq_vec_s vector[2] = {{0, 0}, {0, 0}};

    status = shr_q_addv_wait(q, vector, 1);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_addv_wait(q, NULL, 1);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_wait(q, vector, 0);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_wait(q, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = NULL;
    vector[0].len = 4;
    status = shr_q_addv_wait(tq, vector, 1);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    vector[0].len = 4;
    vector[1].base = "test1";
    vector[1].len = 0;
    status = shr_q_addv_wait(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    vector[1].base = NULL;
    vector[1].len = 5;
    status = shr_q_addv_wait(tq, vector, 2);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}

static void test_addv_timedwait_errors(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    shr_q_s *tq = NULL;
    struct timespec ts = {0};
    sq_vec_s vector[2] = {{0, 0}, {0, 0}};

    status = shr_q_addv_timedwait(q, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    status = shr_q_addv_timedwait(q, NULL, 1, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_timedwait(q, vector, 0, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_timedwait(q, vector, 1, NULL);
    assert(status == SH_ERR_ARG);
    status = shr_q_addv_timedwait(q, vector, 1, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_open(&tq, "testq", SQ_READ_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_STATE);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_open(&tq, "testq", SQ_WRITE_ONLY);
    assert(status == SH_OK);
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    vector[0].base = NULL;
    vector[0].len = 4;
    status = shr_q_addv_timedwait(tq, vector, 1, &ts);
    assert(status == SH_ERR_ARG);
    vector[0].base = "test";
    vector[0].len = 4;
    vector[1].base = "test1";
    vector[1].len = 0;
    status = shr_q_addv_timedwait(tq, vector, 2, &ts);
    assert(status == SH_ERR_ARG);
    vector[1].base = NULL;
    vector[1].len = 5;
    status = shr_q_addv_timedwait(tq, vector, 2, &ts);
    assert(status == SH_ERR_ARG);
    status = shr_q_close(&tq);
    assert(status == SH_OK);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
}


static void test_clean(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    struct timespec limit = {0, 10000000};
    struct timespec sleep = {0, 20000000};
    struct timespec max = {1, 0};
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 1);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    assert(shr_q_clean(NULL, &limit) == SH_ERR_ARG);
    assert(shr_q_clean(q, NULL) == SH_ERR_ARG);
    assert(shr_q_clean(q, &limit) == SH_OK);
    assert(shr_q_count(q) == 0);
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    status = shr_q_add(q, "test2", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    assert(shr_q_clean(q, &max) == SH_OK);
    assert(shr_q_count(q) == 2);
    assert(shr_q_clean(q, &limit) == SH_OK);
    assert(shr_q_count(q) == 0);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_subscription(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;

    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_ALL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONE));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_subscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_ALL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONE));
    assert(shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_unsubscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_ALL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONE));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(!shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_subscribe(q, SQ_EVNT_INIT) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(shr_q_unsubscribe(q, SQ_EVNT_INIT) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_INIT));
    assert(shr_q_subscribe(q, SQ_EVNT_LIMIT) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(shr_q_unsubscribe(q, SQ_EVNT_LIMIT) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LIMIT));
    assert(shr_q_subscribe(q, SQ_EVNT_EMPTY) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(shr_q_unsubscribe(q, SQ_EVNT_EMPTY) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_EMPTY));
    assert(shr_q_subscribe(q, SQ_EVNT_NONEMPTY) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(shr_q_unsubscribe(q, SQ_EVNT_NONEMPTY) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_NONEMPTY));
    assert(shr_q_subscribe(q, SQ_EVNT_LEVEL) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(shr_q_unsubscribe(q, SQ_EVNT_LEVEL) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_LEVEL));
    assert(shr_q_subscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(shr_q_is_subscribed(q, SQ_EVNT_TIME));
    assert(shr_q_unsubscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(!shr_q_is_subscribed(q, SQ_EVNT_TIME));
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_empty_queue(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;

    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_NONE);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_EMPTY);
    assert(item.buffer == NULL);
    assert(item.buf_size == 0);
    adds = 0;
    status = shr_q_call(q, SIGUSR1);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_ERR_EMPTY);
    assert(adds == 1);
    item = shr_q_remove_timedwait(q, &item.buffer, &item.buf_size, &(struct timespec) {0, 10000000});
    assert(item.status == SH_ERR_EMPTY);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(item.buffer);
}


static void test_single_item_queue(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(shr_q_listen(q, SIGUSR1) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_add(q, "test", 4) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_INIT);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    assert(shr_q_add(q, "test", 4) == SH_ERR_LIMIT);
    assert(shr_q_event(q) == SQ_EVNT_LIMIT);
    assert(shr_q_count(q) == 1);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 4);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    free(item.buffer);
    assert(events == 5);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

static void test_multi_item_queue(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 2, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_ALL) == SH_OK);
    assert(shr_q_listen(q, SIGUSR1) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_INIT);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_count(q) == 2);
    assert(shr_q_add(q, "test", 4) == SH_ERR_LIMIT);
    assert(shr_q_event(q) == SQ_EVNT_LIMIT);
    assert(shr_q_count(q) == 2);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 1);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_NONEMPTY);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_EMPTY);
    free(item.buffer);
    assert(events == 5);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}


static void test_expiration_discard(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;
    struct timespec sleep = {0, 200000000};

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_will_discard(q) == false);
    assert(shr_q_timelimit(q, 0, 50000000) == SH_OK);
    assert(shr_q_will_discard(q) == false);
    assert(shr_q_discard(q, true) == SH_OK);
    assert(shr_q_will_discard(q) == true);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 1);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_TIME);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(item.buffer);
}


static void test_codel_algorithm(void)
{
    sh_status_e status;
    sq_item_s item = {0};
    shr_q_s *q = NULL;
    struct timespec sleep = {0, 100000000};

    adds = 0;
    events = 0;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(q != NULL);
    assert(shr_q_subscribe(q, SQ_EVNT_TIME) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_will_discard(q) == false);
    assert(shr_q_timelimit(q, 0, 100000000) == SH_OK);
    assert(shr_q_target_delay(q, 0, 5000000) == SH_OK);
    assert(shr_q_will_discard(q) == true);
    status = shr_q_add(q, "test", 4);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 1);
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    status = shr_q_add(q, "test1", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 2);
    sleep.tv_sec = 0;
    sleep.tv_nsec = 10000000;
    while (nanosleep(&sleep, &sleep) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
    status = shr_q_add(q, "test2", 5);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 3);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_event(q) == SQ_EVNT_TIME);
    assert(shr_q_event(q) == SQ_EVNT_TIME);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
    free(item.buffer);
}


static void test_vector_operations(void)
{
    sh_status_e status;
    shr_q_s *q = NULL;
    sq_item_s item = {0};
    struct timespec ts = {0};
    sq_vec_s vector[2] = {{0}, {0}};

    vector[0].type = SH_ASCII_T;
    vector[1].type = SH_ASCII_T;
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    vector[0].base = "token";
    vector[0].len = 5;
    status = shr_q_addv(q, vector, 1);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "token", 5) == 0);
    assert(item.vector != NULL);
    assert(item.vcount == 1);
    assert(item.vector[0].type == SH_ASCII_T);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    vector[1].base = "test1";
    vector[1].len = 5;
    status = shr_q_addv(q, vector, 2);
    assert(status == SH_OK);
    vector[1].base = "test2";
    vector[1].len = 5;
    status = shr_q_addv_wait(q, vector, 2);
    assert(status == SH_OK);
    vector[1].base = "test3";
    vector[1].len = 5;
    status = shr_q_addv_timedwait(q, vector, 2, &ts);
    assert(status == SH_OK);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length != 0);
    assert(item.value != NULL);
    assert(item.vector != NULL);
    assert(item.vcount == 2);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    assert(item.vector[1].len == 5);
    assert(memcmp(item.vector[1].base, "test1", 5) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length != 0);
    assert(item.value != NULL);
    assert(item.vector != NULL);
    assert(item.vcount == 2);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    assert(item.vector[1].len == 5);
    assert(memcmp(item.vector[1].base, "test2", 5) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length != 0);
    assert(item.value != NULL);
    assert(item.vector != NULL);
    assert(item.vcount == 2);
    assert(item.vector[0].type == SH_ASCII_T);
    assert(item.vector[0].len == 5);
    assert(memcmp(item.vector[0].base, "token", 5) == 0);
    assert(item.vector[0].type == SH_ASCII_T);
    assert(item.vector[1].len == 5);
    assert(memcmp(item.vector[1].base, "test3", 5) == 0);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}


void test_adaptive_lifo()
{
    sh_status_e status;
    shr_q_s *q = NULL;
    sq_item_s item = {0};
    shm_unlink("testq");
    status = shr_q_create(&q, "testq", 0, SQ_READWRITE);
    assert(status == SH_OK);
    assert(shr_q_count(q) == 0);
    assert(!shr_q_will_lifo(q));
    assert(shr_q_limit_lifo(q, true) == SH_OK);
    assert(shr_q_will_lifo(q));
    assert(shr_q_limit_lifo(q, false) == SH_OK);
    assert(!shr_q_will_lifo(q));
    assert(shr_q_limit_lifo(q, true) == SH_OK);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_count(q) == 3);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_level(q, 2) == SH_OK);
    assert(shr_q_count(q) == 0);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_add(q, "test2", 5) == SH_OK);
    assert(shr_q_add(q, "test3", 5) == SH_OK);
    assert(shr_q_add(q, "test4", 5) == SH_OK);
    assert(shr_q_count(q) == 4);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test4", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test3", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test2", item.length) == 0);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    free(item.buffer);
}

int main(void)
{
    set_signal_handlers();

    /*
        Test functions
    */
    test_CAS();
    test_DWCAS();
    test_create_error_paths();
    test_create_namedq();
    test_alloc_node_slots();
    test_monitor();
    test_listen();
    test_call();
    test_free_data_slots();
    test_first_fit_allocation();
    test_large_data_allocation();
    test_add_errors();
    test_add_wait_errors();
    test_add_timedwait_errors();
    test_remove_errors();
    test_remove_wait_errors();
    test_remove_timedwait_errors();
    test_is_valid();
    test_addv_errors();
    test_addv_wait_errors();
    test_addv_timedwait_errors();

    // set up to test open and close

    shr_q_s *q;
    sh_status_e status = shr_q_create(&q, "testq", 1, SQ_IMMUTABLE);
    assert(status == SH_OK);
    test_open_close();
    test_add();
    status = shr_q_destroy(&q);
    assert(status == SH_OK);

    /*
        Test behaviors
    */
    test_subscription();
    test_empty_queue();
    test_single_item_queue();
    test_multi_item_queue();
    test_clean();
    test_vector_operations();
    test_expiration_discard();
    test_codel_algorithm();
    test_adaptive_lifo();

    return 0;
}

#endif // TESTMAIN

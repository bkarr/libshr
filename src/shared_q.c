/*
The MIT License (MIT)

Copyright (c) 2015 Bryan Karr

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define SHR_OBJ_DIR "/dev/shm/"
#define SHRQ "shrq"

#define timespecadd(a, b, result)                           \
    do {                                                    \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;       \
        (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;    \
        if ((result)->tv_nsec >= 1000000000L) {             \
            (result)->tv_sec++;                             \
            (result)->tv_nsec -= 1000000000L;               \
        }                                                   \
    } while(0)

enum shr_q_constants
{
    PAGE_SIZE = 4096,   // initial size of memory mapped file for queue
    NODE_SIZE = 4,      // node slot count
    DATA_HDR = 4,       // data header
    EVENT_OFFSET = 2,   // offset in node for event for queued item
    VALUE_OFFSET = 3,   // offset in node for data slot for queued item
    DATA_SLOTS = 0,     // total data slots (including header)
    TM_SEC = 1,         // offset for data timestamp seconds value
    TM_NSEC = 2,        // offset for data timestamp nanoseconds value
    DATA_LENGTH = 3,    // offset for data length
};

enum shr_q_disp
{
    TAG = 0,                        // queue identifier tag
    VERSION,                        // implementation version number
    SIZE,                           // size of queue array
    TOTAL,                          // total number of adds to queue
    TAIL,                           // item queue tail
    TAIL_CNT,                       // item queue tail counter
    FREE_HEAD,                      // free node list head
    FREE_HD_CNT,                    // free node head counter
    TS_SEC,                         // timestamp of last add in seconds
    TS_NSEC,                        // timestamp of last add in nanoseconds
    NODE_ALLOC,                     // next available node allocation slot
    DATA_ALLOC,                     // next available data allocation slot
    EVENT_HEAD,                     // event queue head
    EVENT_HD_CNT,                   // event queue head counter
    ROOT_FREE,                      // root of free data index
    ROOT_RDX,                       // radix info for root
    EVENT_TAIL,                     // event queue tail
    EVENT_TL_CNT,                   // event queue tail counter
    AVG_LMT_SEC,                    // average time limit in seconds
    AVG_LMT_NSEC,                   // average time limit in nanoseconds
    TOTAL_SEC,                      // total seconds of items on queue
    TOTAL_NSEC,                     // total nanoseconds of items on queue
    HEAD,                           // item queue head
    HEAD_CNT,                       // item queue head counter
    FREE_TAIL,                      // free node list tail
    FREE_TL_CNT,                    // free node tail counter
    LEVEL,                          // queue depth event level
    PAD,                            // padding
    LISTEN_PID,                     // arrival notification process id
    LISTEN_SIGNAL,                  // arrival notification signal
    LIMIT_SEC,                      // time limit in seconds
    LIMIT_NSEC,                     // time limit in nanoseconds
    NOTIFY_PID,                     // event notification process id
    NOTIFY_SIGNAL,                  // event notification signal
    READ_SEM,                       // read semaphore
    WRITE_SEM = (READ_SEM + 4),     // write semaphore
    IO_SEM = (WRITE_SEM + 4),       // i/o semaphore
    AVAIL = (IO_SEM +4),            // next avail free slot
    HDR_END = (AVAIL + 18),         // end of queue header
};


/*
    reference to critbit trie node
*/
typedef struct idx_ref
{
    int64_t next;
    union {
        struct {
            int8_t  flag;
            uint8_t bits;
            int32_t byte;
        };
        int64_t diff;
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
        uint8_t key[8];
        int64_t count;
    };
    int64_t pad;
    int64_t allocs;
    int64_t allocs_count;
} idx_leaf_s;


/*
    structure for managing mmapped data
*/
typedef struct extent
{
    struct extent *next;
    int64_t *array;
    int64_t size;
    int64_t slots;
} extent_s;

/*

*/
typedef struct view
{
    sh_status_e status;
    int64_t slot;
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
    int64_t accessors;
};


typedef struct dword {
    uint64_t low;
    uint64_t high;
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


static sh_status_e free_data_gap(shr_q_s *q, int64_t slot, int64_t count);
static view_s insure_in_range(shr_q_s *q, int64_t slot);

/*==============================================================================

    private functions

==============================================================================*/


/*
    AFS64 -- atomic fetch and subtract 64-bit
*/
static inline int64_t AFS64(
    volatile int64_t *mem,
    int64_t add
)   {
    add = -add;
    __asm__ __volatile__("lock; xaddq %0,%1"
    :"+r" (add),
    "+m" (*mem)
    : : "memory");
    return add;
}


/*
    AFA64 -- atomic fetch and add 64-bit
*/
static inline int64_t AFA64(
    volatile int64_t *mem,
    int64_t add
)   {
    __asm__ __volatile__("lock; xaddq %0,%1"
    :"+r" (add),
    "+m" (*mem)
    : : "memory");
    return add;
}


/*
    CAS -- atomic compare and swap
*/
static inline char CAS(
    volatile intptr_t *mem,
    intptr_t old, intptr_t new
)   {
    int32_t old_h = old >> 32, old_l = (int32_t)old;
    int32_t new_h = new >> 32, new_l = (int32_t)new;

    char r = 0;
    __asm__ __volatile__("lock; cmpxchg8b (%6);"
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


/*
    DWCAS -- atomic double word compare and swap
*/
static inline char DWCAS(
    volatile DWORD *mem,
    DWORD *old,
    DWORD *new
)   {
    uint64_t  old_h = old->high, old_l = old->low;
    uint64_t  new_h = new->high, new_l = new->low;

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


static sh_status_e init_queue(
    shr_q_s **q,                // address of q struct pointer -- not NULL
    uint32_t max_depth          // max depth allowed at which add is blocked
)   {
    extent_s *extent = (*q)->current;

    extent->size = PAGE_SIZE;
    extent->slots = (int64_t)PAGE_SIZE >> 3;
    extent->array[SIZE] = extent->slots;

    memcpy(&extent->array[TAG], SHRQ, sizeof(SHRQ) - 1);

    int rc = sem_init((sem_t*)&extent->array[READ_SEM], 1, 0);
    if (rc < 0) {
        free(*q);
        *q = NULL;
        return SH_ERR_NOSUPPORT;
    }
    rc = sem_init((sem_t*)&extent->array[WRITE_SEM], 1, max_depth);
    if (rc < 0) {
        free(*q);
        *q = NULL;
        return SH_ERR_NOSUPPORT;
    }
    // IO_SEM acts as shared memory mutex for resizing mmapped file
    rc = sem_init((sem_t*)&extent->array[IO_SEM], 1, 1);
    if (rc < 0) {
        free(*q);
        *q = NULL;
        return SH_ERR_NOSUPPORT;
    }

    extent->array[NODE_ALLOC] = HDR_END + (3 * NODE_SIZE);
    extent->array[DATA_ALLOC] = extent->array[SIZE];
    extent->array[FREE_HEAD] = HDR_END;
    extent->array[FREE_HD_CNT] = (long)UINT_MAX * 2;
    extent->array[FREE_TAIL] = HDR_END;
    extent->array[FREE_TL_CNT] = (long)UINT_MAX * 2;
    extent->array[EVENT_HEAD] = HDR_END + NODE_SIZE;
    extent->array[EVENT_HD_CNT] = 0;
    extent->array[EVENT_TAIL] = HDR_END + NODE_SIZE;
    extent->array[EVENT_TL_CNT] = 0;
    extent->array[HEAD] = HDR_END + (2 * NODE_SIZE);
    extent->array[HEAD_CNT] = UINT_MAX;
    extent->array[TAIL] = HDR_END + (2 * NODE_SIZE);
    extent->array[TAIL_CNT] = UINT_MAX;
    extent->array[extent->array[FREE_HEAD]] = HDR_END;
    extent->array[extent->array[FREE_HEAD] + 1] = (long)UINT_MAX * 2;
    extent->array[extent->array[EVENT_HEAD]] = HDR_END + NODE_SIZE;
    extent->array[extent->array[EVENT_HEAD] + 1] = 0;
    extent->array[extent->array[HEAD]] = HDR_END + (2 * NODE_SIZE);
    extent->array[extent->array[HEAD] + 1] = UINT_MAX;

    return SH_OK;
}


static sh_status_e create_named_queue(
    shr_q_s **q,            // address of q struct pointer -- not NULL
    char *name,             // name of q as null terminated string, or NULL
    uint32_t max_depth,     // max depth allowed at which add is blocked
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
        free(*q);
        *q = NULL;
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

    return init_queue(q, max_depth);
}


static void add_end(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    int64_t slot,       // slot reference
    int64_t tail        // tail slot of list
)   {
    DWORD next_after;
    DWORD tail_before;
    DWORD tail_after;
    int64_t next;
    view_s view = {.extent = q->current};
    int64_t *array = view.extent->array;

    array[slot] = slot;
    next_after.low = slot;

    while(true) {
        tail_before.high = array[tail + 1];
        next = array[tail];
        tail_before.low = next;
        array[slot + 1] = tail_before.high + 1;
        next_after.high = tail_before.high + 1;
        view = insure_in_range(q, next);
        array = view.extent->array;
        if (DWCAS((DWORD*)&array[next], &tail_before, &next_after)) {
            DWCAS((DWORD*)&array[tail], &tail_before, &next_after);
            break;
        } else {
            tail_after.high = tail_before.high + 1;
            tail_after.low = array[next];
            DWCAS((DWORD*)&array[tail], &tail_before, &tail_after);
        }
    }
}


static int64_t calculate_realloc_size(
    extent_s *extent,   // pointer to current extent -- not NULL
    int64_t slots       // number of slots to allocate
)   {
    int64_t current_pages = extent->size >> 12;
    int64_t needed_pages = ((slots << 3) >> 12) + 1;
    int64_t exp_pages = (extent->size >> 2) >> 12;

    if (needed_pages > exp_pages) {
        return (current_pages + needed_pages) * PAGE_SIZE;
    }
    return (current_pages + exp_pages) * PAGE_SIZE;
}


static view_s resize_extent(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    extent_s *extent    // pointer to current extent -- not NULL
)   {
    view_s view = {.status = SH_OK, .extent = q->current};
    int64_t *array = view.extent->array;
    if (extent != view.extent) {
        return view;
    }

    if (extent->slots == array[SIZE]) {
        return view;
    }

    extent_s *next = calloc(1, sizeof(extent_s));
    if (next == NULL) {
        view.status = SH_ERR_NOMEM;
        return view;
    }

    next->slots = array[SIZE];
    next->size = next->slots << 3;

    // extend mapped array
    next->array = mmap(0, next->size, q->prot, q->flags, q->fd, 0);
    if ((char*)next->array == (char *)-1) {
        free(next);
        view.status = SH_ERR_NOMEM;
        view.extent = q->current;
        return view;
    }

    // add to end of list of extents
    extent_s *tail = view.extent;
    if (CAS((intptr_t*)&tail->next, (intptr_t)NULL, (intptr_t)next)) {
        CAS((intptr_t*)&q->current, (intptr_t)tail, (intptr_t)next);
    } else {
        CAS((intptr_t*)&q->current, (intptr_t)tail, (intptr_t)tail->next);
        munmap(next->array, next->size);
        free(next);
    }

    view.extent = q->current;
    return view;
}


static view_s expand(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    extent_s *extent,   // pointer to current extent -- not NULL
    int64_t slots       // number of slots to allocate
)   {
    view_s view = {.status = SH_OK, .extent = extent};
    int64_t *array = extent->array;
    int64_t size = calculate_realloc_size(extent, slots);
    DWORD before;
    DWORD after;

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
            array[SIZE] = size >> 3;

            // update allocation values
            after.high = array[SIZE];
            do {
                before.low = array[NODE_ALLOC];
                before.high = array[DATA_ALLOC];
                if (before.high == extent->slots) {
                    after.low = before.low;
                } else {
                    after.low = extent->slots;
                }
            } while (!DWCAS((DWORD*)&array[NODE_ALLOC], &before, &after));

            if (before.low != after.low && before.high - before.low >= 2) {
                free_data_gap(q, before.low, before.high - before.low);
            }
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


static view_s insure_in_range(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot
)   {
    view_s view = {.status = SH_OK, .slot = 0, .extent = q->current};
    if (slot < HDR_END) {
        return view;
    }
    if (slot >= view.extent->slots) {
        view = resize_extent(q, view.extent);
    }
    view.slot = slot;
    return view;
}


static int64_t remove_front(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    int64_t ref,        // expected slot number -- 0 if no interest
    int64_t gen,        // generation count
    int64_t head,       // head slot of list
    int64_t tail        // tail slot of list
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    int64_t *array = view.extent->array;
    DWORD before;
    DWORD after;

    if (ref >= HDR_END && ref != array[tail]) {
        view = insure_in_range(q, ref);
        array = view.extent->array;
        after.low = array[ref];
        before.high = gen;
        after.high = before.high + 1;
        before.low = (uint64_t)ref;
        if (DWCAS((DWORD*)&array[head], &before, &after)) {
            memset(&array[ref], 0, 2 << 3);
            return ref;
        }
    }

    return 0;
}


static view_s alloc_node_slots(
    shr_q_s *q          // pointer to queue struct
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    int64_t *array = view.extent->array;
    DWORD before;
    DWORD after;
    int64_t slots = NODE_SIZE;
    int64_t node_alloc;
    int64_t data_alloc;
    int64_t alloc_end;

    while (true) {
        // attempt to remove from free node list
        int64_t gen = array[FREE_HD_CNT];
        node_alloc = array[FREE_HEAD];
        while (node_alloc != array[FREE_TAIL]) {
            node_alloc = remove_front(q, node_alloc, gen, FREE_HEAD, FREE_TAIL);
            view = insure_in_range(q, node_alloc);
            array = view.extent->array;
            if (view.slot != 0) {
                memset(&array[node_alloc], 0, slots << 3);
                return view;
            }
            gen = array[FREE_HD_CNT];
            node_alloc = array[FREE_HEAD];
        }

        // attempt to allocate new node from current extent
        node_alloc = array[NODE_ALLOC];
        data_alloc = array[DATA_ALLOC];
        alloc_end = node_alloc + slots;
        view = insure_in_range(q, node_alloc);
        array = view.extent->array;
        if (alloc_end < data_alloc) {
            before.low = node_alloc;
            before.high = data_alloc;
            after.low = alloc_end;
            after.high = data_alloc;
            if (DWCAS((DWORD*)&array[NODE_ALLOC], &before, &after)) {
                memset(array+node_alloc, 0, slots << 3);
                view.slot = node_alloc;
                return view;
            }
            view.extent = q->current;
            array = view.extent->array;
            continue;
        }

        view = expand(q, view.extent, slots);
        if (view.status != SH_OK) {
            return view;
        }
    }
    return view;
}


static int64_t find_leaf(
    shr_q_s *q,         // pointer to queue struct
    int64_t count,      // number of slots to return
    int64_t ref_index   // index node reference to begin search
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    int64_t *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
    uint8_t *key = (uint8_t*)&count;
    int64_t node_slot = ref_index;

    while (ref->flag < 0) {
        node_slot = ref->next;
        view = insure_in_range(q, node_slot);
        if (view.slot == 0) {
            return 0;
        }
        array = view.extent->array;
        idx_node_s *node = (idx_node_s*)&array[node_slot];
        int64_t direction = (1 + (ref->bits | key[ref->byte])) >> 8;
        ref = &node->child[direction];
    }
    view = insure_in_range(q, ref->next);
    if (view.slot == 0) {
        return 0;
    }
    return ref->next;
}


static sh_status_e add_idx_gap(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot,       // start of slot range
    int64_t count,      // number of slots to return
    int64_t ref_index
)   {
    if (count < NODE_SIZE + 2) {
        if (count >= NODE_SIZE) {
            add_end(q, slot, FREE_TAIL);
        }
        return SH_OK;
    }
    int64_t node = slot;
    slot += NODE_SIZE;
    count -= NODE_SIZE;

    extent_s *extent = q->current;
    int64_t *array = extent->array;
    idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
    idx_leaf_s *leaf = (idx_leaf_s*)&array[node];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;
    array[slot] = 0;
    array[slot + 1] = 0;
    DWORD before = {0, 0};
    DWORD after = { .low = node, .high = 0};

    if (!DWCAS((DWORD*)ref, &before, &after)) {
        add_end(q, node, FREE_TAIL);
        return SH_RETRY;
    }
    return SH_OK;
}


static sh_status_e add_to_leaf(
    shr_q_s *q,         // pointer to queue struct
    idx_leaf_s *leaf,
    int64_t slot
)   {
    view_s view = insure_in_range(q, slot);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }

    int64_t *array = view.extent->array;
    DWORD before = {0};
    DWORD after = {0};

    after.low = slot;
    do {
        array[slot + 1] = leaf->allocs_count;
        array[slot] = leaf->allocs;
        before.high = array[slot + 1];
        before.low = array[slot];
        after.high = before.high + 1;
        after.low = slot;
    } while (!DWCAS((DWORD*)&leaf->allocs, &before, &after));

    return SH_OK;
}


static sh_status_e insert_idx_gap(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot,       // start of slot range
    int64_t count,      // number of slots to return
    int64_t ref_index
)   {
    if (count < (2 * NODE_SIZE) + 2) {
        // abandon gap as too small to track
        return SH_OK;
    }

    // create new leaf
    int64_t leaf_index = slot;
    slot += NODE_SIZE;
    count -= NODE_SIZE;

    // create new internal node
    int64_t node_index = slot;
    slot += NODE_SIZE;
    count -= NODE_SIZE;

    int64_t index = find_leaf(q, count, ROOT_FREE);
    if (index == 0) {
        return SH_ERR_NOMEM;
    }
    view_s view = insure_in_range(q, index);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }
    idx_leaf_s *leaf = (idx_leaf_s*)&view.extent->array[index];

    // evaluate differences in keys
    int64_t byte = 0;
    uint8_t bits = 0;
    uint8_t *key = (uint8_t*)&count;

    /*
        compared right to left because integers in x86_64 are little endian
    */
    for (byte = sizeof(int64_t) - 1; byte >= 0 ; byte--) {
        bits = key[byte]^leaf->key[byte];
        if (bits) {
            break;
        }
    }
    if (bits == 0) {
        // keys are equal
        add_end(q, leaf_index, FREE_TAIL);
        add_end(q, node_index, FREE_TAIL);
        return add_to_leaf(q, leaf, slot);
    }

    // calculate bit difference
    bits = (uint8_t)~(1 << (31 - __builtin_clz(bits)));
    int64_t newdirection = (1 + (bits | key[byte])) >> 8;

    //initialize new leaf
    int64_t *array = view.extent->array;
    array[slot] = 0;
    array[slot + 1] = 0;
    leaf = (idx_leaf_s*)&array[leaf_index];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;

    // initialize new internal node
    idx_node_s *node = (idx_node_s*)&array[node_index];
    idx_ref_s *ref = &node->child[newdirection];
    ref->next = leaf_index;

    // find place to insert new node in tree
    view = insure_in_range(q, ref_index);
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
        int64_t direction = ((1 + (parent->bits | key[parent->byte])) >> 8);
        view = insure_in_range(q, parent->next);
        array = view.extent->array;
        node = (idx_node_s*)&array[parent->next];
        parent = &node->child[direction];
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
    if (!DWCAS((DWORD*)parent, &before, &after)) {
        return SH_RETRY;
    }
    return SH_OK;
}


static sh_status_e free_data_gap(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot,       // start of slot range
    int64_t count       // number of slots to return
)   {
    sh_status_e status = SH_RETRY;

    while (status == SH_RETRY) {
        // check if tree is empty
        if (((idx_ref_s*)&q->current->array[ROOT_FREE])->next == 0) {
            status = add_idx_gap(q, slot, count, ROOT_FREE);
            if (status == SH_OK) {
                break;
            }
            continue;
        }

        int64_t index = find_leaf(q, count, ROOT_FREE);
        if (index == 0) {
            return SH_ERR_NOMEM;
        }
        idx_leaf_s *leaf = (idx_leaf_s*)&q->current->array[index];

        // evaluate differences in keys
        uint8_t bits = 0;
        uint8_t *key = (uint8_t*)&count;
        /*
            compared right to left because integers in x86_64 are little endian
        */
        for (int64_t byte = sizeof(int64_t) - 1; byte >= 0 ; byte--) {
            bits = key[byte]^leaf->key[byte];
            if (bits) {
                break;
            }
        }
        if (bits == 0) {
            // keys are equal
            return add_to_leaf(q, leaf, slot);
        }

        status = insert_idx_gap(q, slot, count, ROOT_FREE);
    }
    return status;
}


static sh_status_e add_idx_node(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot,       // start of slot range
    int64_t count,      // number of slots to return
    int64_t ref_index
)   {
    view_s view = alloc_node_slots(q);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }
    int64_t node = view.slot;
    int64_t *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*)&array[ref_index];
    idx_leaf_s *leaf = (idx_leaf_s*)&array[node];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;
    array[slot] = 0;
    array[slot + 1] = 0;
    DWORD before = {0, 0};
    DWORD after = { .low = node, .high = 0};

    if (!DWCAS((DWORD*)ref, &before, &after)) {
        add_end(q, node, FREE_TAIL);
        return SH_RETRY;
    }
    return SH_OK;
}


static sh_status_e insert_idx_node(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot,       // start of slot range
    int64_t count,      // number of slots to return
    int64_t ref_index,
    int64_t byte,
    uint8_t bits
)   {
    // calculate bit difference
    uint8_t *key = (uint8_t*)&count;
    bits = (uint8_t)~(1 << (31 - __builtin_clz(bits)));
    int64_t newdirection = (1 + (bits | key[byte])) >> 8;

    // create and initialize new leaf
    view_s view = alloc_node_slots(q);
    if (view.slot == 0) {
        return SH_ERR_NOMEM;
    }
    int64_t leaf_index = view.slot;
    int64_t *array = view.extent->array;
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
    int64_t node_index = view.slot;
    array = view.extent->array;
    idx_node_s *node = (idx_node_s*)&array[node_index];
    idx_ref_s *ref = &node->child[newdirection];
    ref->next = leaf_index;

    // find place to insert new node in tree
    view = insure_in_range(q, ref_index);
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
        int64_t direction = ((1 + (parent->bits | key[parent->byte])) >> 8);
        view = insure_in_range(q, parent->next);
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
    if (!DWCAS((DWORD*)parent, &before, &after)) {
        add_end(q, leaf_index, FREE_TAIL);
        add_end(q, node_index, FREE_TAIL);
        return SH_RETRY;
    }
    return SH_OK;
}


static sh_status_e free_data_slots(
    shr_q_s *q,         // pointer to queue struct
    int64_t slot,       // start of slot range
    int64_t count       // number of slots to return
)   {
    sh_status_e status = SH_RETRY;

    while (status == SH_RETRY) {
        // check if tree is empty
        if (((idx_ref_s*)&q->current->array[ROOT_FREE])->next == 0) {
            status = add_idx_node(q, slot, count, ROOT_FREE);
            if (status == SH_OK || status != SH_RETRY) {
                break;
            }
            continue;
        }

        int64_t index = find_leaf(q, count, ROOT_FREE);
        if (index == 0) {
            return SH_ERR_NOMEM;
        }
        idx_leaf_s *leaf = (idx_leaf_s*)&q->current->array[index];

        // evaluate differences in keys
        uint8_t bits = 0;
        uint8_t *key = (uint8_t*)&count;
        /*
            compared right to left because integers in x86_64 are little endian
        */
        int64_t byte;
        for (byte = sizeof(int64_t) - 1; byte >= 0 ; byte--) {
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


static int64_t lookup_freed_data(
    shr_q_s *q,         // pointer to queue struct
    int64_t slots       // number of slots to allocate
)   {
    DWORD before;
    DWORD after;

    if (q == NULL || slots < 2) {
        return 0;
    }
    int64_t index = find_leaf(q, slots, ROOT_FREE);
    if (index == 0) {
        return 0;
    }
    view_s view = insure_in_range(q, index);
    int64_t *array = view.extent->array;
    idx_leaf_s *leaf = (idx_leaf_s*)&array[index];
    if (slots != leaf->count) {
        return 0;
    }
    if (leaf->allocs == 0) {
        return 0;
    }
    do {
        before.low = leaf->allocs;
        before.high = leaf->allocs_count;
        view = insure_in_range(q, before.low);
        array = view.extent->array;
        after.low = array[before.low];
        after.high = before.high + 1;
    } while (before.low != 0 && !DWCAS((DWORD*)&leaf->allocs, &before, &after));
    return before.low;
}


static view_s alloc_data_slots(
    shr_q_s *q,         // pointer to queue struct
    int64_t slots       // number of slots to allocate
)   {
    view_s view = {.status = SH_OK, .extent = q->current, .slot = 0};
    int64_t *array = view.extent->array;
    DWORD before;
    DWORD after;
    int64_t node_alloc;
    int64_t data_alloc;
    int64_t alloc_start;

    while (true) {
        if (array[ROOT_FREE] != 0) {
            alloc_start = lookup_freed_data(q, slots);
            view = insure_in_range(q, alloc_start);
            array = view.extent->array;
            if (view.slot != 0) {
                memset(&array[alloc_start], 0, slots << 3);
                return view;
            }
        }

        // attempt to allocate new data slots from current extent
        data_alloc = array[DATA_ALLOC];
        node_alloc = array[NODE_ALLOC];
        alloc_start = data_alloc - slots;
        view = insure_in_range(q, alloc_start);
        array = view.extent->array;
        if (view.slot == alloc_start && alloc_start > node_alloc) {
            before.low = node_alloc;
            before.high = data_alloc;
            after.low = node_alloc;
            after.high = alloc_start;
            if (DWCAS((DWORD*)&array[NODE_ALLOC], &before, &after)) {
                view.slot = alloc_start;
                return view;
            }
            view.extent = q->current;
            array = view.extent->array;
            continue;
        }

        view = expand(q, view.extent, slots);
        if (view.status != SH_OK) {
            return view;
        }
    }
    return view;
}


static int64_t calc_data_slots(
    int64_t length
) {
    int64_t space = DATA_HDR;
    // calculate number of slots needed for data
    space += (length >> 3);
    // account for remainder
    if (length & 7) {
        space++;
    }

    return space;
}


static int64_t copy_value(
    shr_q_s *q,         // pointer to queue struct
    void *value,        // pointer to value data
    int64_t length      // length of data
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return 0;
    }

    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    int64_t space = calc_data_slots(length);

    view_s view = alloc_data_slots(q, space);
    int64_t current = view.slot;
    if (current) {
        int64_t *array = view.extent->array;
        array[current + DATA_SLOTS] = space;
        array[current + TM_SEC] = curr_time.tv_sec;
        array[current + TM_NSEC] = curr_time.tv_nsec;
        array[current + DATA_LENGTH] = length;
        memcpy(&array[current + DATA_HDR], value, length);
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
        if (!CAS((intptr_t*)&q->prev, (intptr_t)head, (intptr_t)next)) {
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
    if (q->current->array[LISTEN_PID] == 0 ||
        q->current->array[LISTEN_SIGNAL] == 0) {
        return;
    }
    int sval = -1;
    (void)sem_getvalue((sem_t*)&q->current->array[READ_SEM], &sval);

    if (sval == 0) {
        kill(q->current->array[LISTEN_PID], q->current->array[LISTEN_SIGNAL]);
    }

}


static void signal_event(
    shr_q_s *q
)   {
    if (q->current->array[NOTIFY_PID] == 0 ||
        q->current->array[NOTIFY_SIGNAL] == 0) {
        return;
    }

    (void)kill(q->current->array[NOTIFY_PID], q->current->array[NOTIFY_SIGNAL]);
}


static void add_event(
    shr_q_s *q,
    sq_event_e event
)   {
    view_s view = {.extent = q->current};

    int64_t *array = view.extent->array;

    if (array[NOTIFY_PID] == 0 || array[NOTIFY_SIGNAL] == 0) {
        return;
    }

    // allocate queue node
    view = alloc_node_slots(q);
    if (view.slot == 0) {
        return;
    }
    array = view.extent->array;

    array[view.slot + EVENT_OFFSET] = event;

    // append node to end of queue
    add_end(q, view.slot, EVENT_TAIL);

    signal_event(q);
}


static sh_status_e enq(
    shr_q_s *q,         // pointer to queue, not NULL
    void *value,        // pointer to item, not NULL
    int64_t length      // length of item
)   {
    int64_t node;
    int64_t data_slot;

    // allocate space and copy value
    data_slot = copy_value(q, value, length);
    if (data_slot == 0) {
        return SH_ERR_NOMEM;
    }
    extent_s *extent = q->current;
    int64_t *array = extent->array;
    struct timespec curr_time;
    curr_time.tv_sec = array[data_slot + TM_SEC];
    curr_time.tv_nsec = array[data_slot + TM_NSEC];

    // allocate queue node
    view_s view = alloc_node_slots(q);
    if (view.slot == 0) {
        free_data_slots(q, data_slot, calc_data_slots(length));
        return SH_ERR_NOMEM;
    }
    node = view.slot;
    array = view.extent->array;

    // point queue node to data slot
    array[node + VALUE_OFFSET] = data_slot;

    // append node to end of queue
    add_end(q, node, TAIL);

    if (AFA64(&array[TOTAL], 1) == 0) {
        add_event(q, SQ_EVNT_INIT);
    }

    signal_arrival(q);

    struct timespec prev_time = *(struct timespec*)&array[TS_SEC];
    DWCAS((DWORD*)&array[TS_SEC], (DWORD*)&prev_time, (DWORD*)&curr_time);

    release_prev_extents(q);

    return SH_OK;
}


static int64_t next_item(
    shr_q_s *q,          // pointer to queue
    int64_t slot
)   {
    view_s view = insure_in_range(q, slot);
    if (view.slot == 0) {
        return 0;
    }
    int64_t *array = view.extent->array;
    int64_t next = array[slot];
    view = insure_in_range(q, next);
    if (view.slot == 0) {
        return 0;
    }
    array = view.extent->array;
    if (next && next < LONG_MAX - EVENT_TAIL) {
        return array[next + VALUE_OFFSET];
    }
    return 0;
}


static bool timelimit_exceeded(
    shr_q_s *q,             // pointer to queue
    int64_t time_secs,      // timestamp seconds
    int64_t time_nsecs      // timestamp nanoseconds
)   {
    extent_s *extent = q->current;
    int64_t *array = extent->array;

    if (array[LIMIT_SEC] == 0 && array[LIMIT_NSEC] == 0) {
        return false;
    }

    if (time_secs > array[LIMIT_SEC]) {
        return true;
    }
    if (time_secs < array[LIMIT_SEC]) {
        return false;
    }
    if (time_nsecs > array[LIMIT_NSEC]) {
        return true;
    }
    if (time_nsecs < array[LIMIT_NSEC]) {
        return false;
    }
    return false;
}


static sq_item_s deq(
    shr_q_s *q,         // pointer to queue
    void **buffer,      // address of buffer pointer, or NULL
    int64_t *buff_size  // pointer to length of buffer if buffer present
)   {
    view_s view = {.extent = q->current};
    int64_t *array = view.extent->array;
    sq_item_s item = {.status = SH_ERR_EMPTY};

    while (true) {
        int64_t gen = array[HEAD_CNT];
        int64_t head = array[HEAD];
        if (head == array[TAIL]) {
            continue;
        }
        view = insure_in_range(q, head);
        array = view.extent->array;
        int64_t data_slot = next_item(q, head);
        if (data_slot == 0 || remove_front(q, head, gen, HEAD, TAIL) == 0) {
            continue;   // try again
        }
        // free queue node
        add_end(q, head, FREE_TAIL);
        // insure data is accessible
        view = insure_in_range(q, data_slot);
        if (view.slot == 0) {
            break;
        }
        array = view.extent->array;
        int64_t end_slot = data_slot + array[data_slot + DATA_SLOTS] - 1;
        view = insure_in_range(q, end_slot);
        if (view.slot == 0) {
            break;
        }
        array = view.extent->array;

        // copy data to buffer
        int64_t size = (array[data_slot + DATA_SLOTS] << 3) - sizeof(int64_t);
        if (*buffer && *buff_size < size) {
            free(*buffer);
            *buffer = NULL;
            *buff_size = 0;
        }
        if (*buffer == NULL) {
            *buffer = malloc(size);
            *buff_size = size;
            if (*buffer == NULL) {
                item.status = SH_ERR_NOMEM;
                break;
            }
        }

        memcpy(*buffer, &array[data_slot + 1], size);
        item.buffer = *buffer;
        item.buf_size = size;
        item.length = array[data_slot + DATA_LENGTH];
        item.timestamp = *buffer;
        item.value = (uint8_t*)*buffer + (3 * sizeof(int64_t));

        if (timelimit_exceeded(q, array[data_slot + TM_SEC],
                array[data_slot + TM_NSEC])) {
            add_event(q, SQ_EVNT_TIME);
        }

        free_data_slots(q, data_slot, array[data_slot]);
        item.status = SH_OK;
        break;
    }

    release_prev_extents(q);

    return item;
}


static sq_event_e next_event(
    shr_q_s *q,          // pointer to queue
    int64_t slot
)   {
    view_s view = insure_in_range(q, slot);
    if (view.slot == 0) {
        return SQ_EVNT_NONE;
    }
    int64_t *array = view.extent->array;

    int64_t next = array[slot];
    view = insure_in_range(q, next);
    if (view.slot == 0) {
        return SQ_EVNT_NONE;
    }
    array = view.extent->array;
    return array[next + EVENT_OFFSET];
}


static void check_level(
    shr_q_s *q          // pointer to queue
)   {
    (void)AFA64(&q->accessors, 1);
    view_s view = {.extent = q->current};
    int64_t *array = view.extent->array;

    int64_t level = array[LEVEL];
    if (level <= 0) {
        (void)AFS64(&q->accessors, 1);
        return;
    }

    int sval = 0;
    if (sem_getvalue((sem_t*)&q->current->array[READ_SEM], &sval) < 0) {
        (void)AFS64(&q->accessors, 1);
        return;
    }
    if (sval < level) {
        (void)AFS64(&q->accessors, 1);
        return;
    }

    if (!CAS(&array[LEVEL], level, 0)) {
        (void)AFS64(&q->accessors, 1);
        return;
    }

    // allocate queue node
    view = alloc_node_slots(q);
    if (view.slot == 0) {
        (void)AFS64(&q->accessors, 1);
        return;
    }
    array = view.extent->array;
    int64_t node = view.slot;

    array[node + EVENT_OFFSET] = SQ_EVNT_LEVEL;

    // append node to end of queue
    add_end(q, node, EVENT_TAIL);

    signal_event(q);

    (void)AFS64(&q->accessors, 1);

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
    char *name,             // name of q as null terminated string -- not NULL
    uint32_t max_depth,     // max depth allowed at which add is blocked
    sq_mode_e mode          // read/write mode
)   {
    if (q == NULL ||
        name == NULL ||
        max_depth > SEM_VALUE_MAX) {
        return SH_ERR_ARG;
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
    shr_q_s **q,        // address of q struct pointer -- not NULL
    char *name,         // name of q as a null terminated string, or NULL
    sq_mode_e mode      // read/write mode
)   {
    if (q == NULL || name == NULL) {
        return SH_ERR_ARG;
    }

    if (strlen(name) > PATH_MAX) {
        return SH_ERR_PATH;
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
    size_t size = statbuf.st_size;
    do {
        (*q)->current->array = mmap(0, size, prot, (*q)->flags, (*q)->fd, 0);
        if ((*q)->current->array == (void*)-1) {
            free((*q)->current);
            free(*q);
            *q = NULL;
            return convert_to_status(errno);
        }
        if (size == (*q)->current->array[SIZE] << 3) {
            break;
        }
        size_t alt_size = (*q)->current->array[SIZE] << 3;
        munmap((*q)->current->array, size);
        size = alt_size;
    } while (true);
    (*q)->prot = prot;
    (*q)->current->size = size;
    (*q)->current->slots = size >> 3;

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
    SH_ERR_STATE    if registering and pid is not zero or original pid,
                    or unregistering and pid does not match
*/
extern sh_status_e shr_q_monitor(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for event notification
)   {
    if (q == NULL || signal < 0) {
        return SH_ERR_ARG;
    }
    (void)AFA64(&q->accessors, 1);

    int pid = getpid();
    if (signal == 0) {
        if (pid != q->current->array[NOTIFY_PID]) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }

        if (CAS(&q->current->array[NOTIFY_PID], pid, 0)) {
            q->current->array[NOTIFY_SIGNAL] = signal;
            (void)AFS64(&q->accessors, 1);
            return SH_OK;
        }
        (void)AFS64(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    if (q->current->array[NOTIFY_PID] != 0 &&
        q->current->array[NOTIFY_PID] != pid) {
        (void)AFS64(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    int64_t prev = q->current->array[NOTIFY_PID];
    if (CAS(&q->current->array[NOTIFY_PID], prev, pid)) {
        q->current->array[NOTIFY_SIGNAL] = signal;
        (void)AFS64(&q->accessors, 1);
        return SH_OK;
    }

    (void)AFS64(&q->accessors, 1);
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
    SH_ERR_STATE    if registering and pid is not zero, or unregistering and pid
                    does not match
*/
extern sh_status_e shr_q_listen(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for event notification
)   {
    if (q == NULL || signal < 0) {
        return SH_ERR_ARG;
    }
    (void)AFA64(&q->accessors, 1);

    int pid = getpid();
    if (signal == 0) {
        if (pid != q->current->array[LISTEN_PID]) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }

        if (CAS(&q->current->array[LISTEN_PID], pid, 0)) {
            q->current->array[LISTEN_SIGNAL] = signal;
            (void)AFS64(&q->accessors, 1);
            return SH_OK;
        }
        (void)AFS64(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    if (q->current->array[LISTEN_PID] != 0 &&
        q->current->array[LISTEN_PID] != pid) {
        (void)AFS64(&q->accessors, 1);
        return SH_ERR_STATE;
    }

    int64_t prev = q->current->array[LISTEN_PID];
    if (CAS(&q->current->array[LISTEN_PID], prev, pid)) {
        q->current->array[LISTEN_SIGNAL] = signal;
        (void)AFS64(&q->accessors, 1);
        return SH_OK;
    }

    (void)AFS64(&q->accessors, 1);
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
    int64_t length      // length of item -- greater than 0
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA64(&q->accessors, 1);

    while (sem_trywait((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
        if (errno == EAGAIN) {
            add_event(q, SQ_EVNT_DEPTH);
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_LIMIT;
        }
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status = enq(q, value, length);

    while (sem_post((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            status = SH_ERR_STATE;
            return status;
        }
    }

    check_level(q);

    (void)AFS64(&q->accessors, 1);
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
    int64_t length      // length of item -- greater than 0
)   {
    if (q == NULL || value == NULL || length <= 0) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA64(&q->accessors, 1);

    int sval = 0;
    if (sem_getvalue((sem_t*)&q->current->array[WRITE_SEM], &sval) == 0 &&
        sval == 0) {
        add_event(q, SQ_EVNT_DEPTH);
    }

    while (sem_wait((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status = enq(q, value, length);

    while (sem_post((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    check_level(q);

    (void)AFS64(&q->accessors, 1);
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
    int64_t length,             // length of item -- greater than 0
    struct timespec *timeout    // timeout value -- not NULL
)   {
    if (q == NULL || value == NULL || length <= 0 || timeout == NULL) {
        return SH_ERR_ARG;
    }

    if (!(q->mode & SQ_WRITE_ONLY)) {
        return SH_ERR_STATE;
    }
    (void)AFA64(&q->accessors, 1);

    int sval = 0;
    if (sem_getvalue((sem_t*)&q->current->array[WRITE_SEM], &sval) == 0 &&
        sval == 0) {
        add_event(q, SQ_EVNT_DEPTH);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    timespecadd(&ts, timeout, &ts);
    while (sem_timedwait((sem_t*)&q->current->array[WRITE_SEM], &ts) < 0) {
        if (errno == ETIMEDOUT) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_LIMIT;
        }
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    sh_status_e status = enq(q, value, length);

    while (sem_post((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return SH_ERR_STATE;
        }
    }

    check_level(q);

    (void)AFS64(&q->accessors, 1);
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
    int64_t *buff_size  // pointer to size of buffer -- not NULL
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

    (void)AFA64(&q->accessors, 1);

    while (sem_trywait((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EAGAIN) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_EMPTY};
        }
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_STATE};
        }
    }

    sq_item_s item = deq(q, buffer, buff_size);

    while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_STATE};
        }
    }

    (void)AFS64(&q->accessors, 1);
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
    int64_t *buff_size      // pointer to size of buffer -- not NULL
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

    (void)AFA64(&q->accessors, 1);

    while (sem_wait((sem_t*)&q->current->array[READ_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_STATE};
        }
    }

    sq_item_s item = deq(q, buffer, buff_size);

    while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_STATE};
        }
    }

    (void)AFS64(&q->accessors, 1);
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
    int64_t *buff_size,         // pointer to size of buffer -- not NULL
    struct timespec *timeout    // timeout value
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
    (void)AFA64(&q->accessors, 1);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    timespecadd(&ts, timeout, &ts);
    while (sem_timedwait((sem_t*)&q->current->array[READ_SEM], &ts) < 0) {
        if (errno == ETIMEDOUT) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_EMPTY};
        }
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_STATE};
        }
    }

    sq_item_s item = deq(q, buffer, buff_size);

    while (sem_post((sem_t*)&q->current->array[WRITE_SEM]) < 0) {
        if (errno == EINVAL) {
            (void)AFS64(&q->accessors, 1);
            return (sq_item_s){.status = SH_ERR_STATE};
        }
    }

    (void)AFS64(&q->accessors, 1);
    return item;
}


/*
    shr_q_event -- returns active event or SQ_EVENT_NONE when empty

*/
extern sq_event_e shr_q_event(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return SQ_EVNT_NONE;
    }
    (void)AFA64(&q->accessors, 1);

    extent_s *extent = q->current;
    int64_t *array = extent->array;
    sq_event_e event = SQ_EVNT_NONE;

    int64_t gen = array[EVENT_HD_CNT];
    int64_t head = array[EVENT_HEAD];
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

    (void)AFS64(&q->accessors, 1);
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
    int64_t lim_secs,           // time limit in seconds
    int64_t lim_nsecs           // time limie in nanoseconds
)   {
    if (q == NULL) {
        return false;
    }
    (void)AFA64(&q->accessors, 1);

    extent_s *extent = q->current;
    int64_t *array = extent->array;
    struct timespec curr_time;
    (void)clock_gettime(CLOCK_REALTIME, &curr_time);

    if (curr_time.tv_sec - array[TS_SEC] > lim_secs) {
        (void)AFS64(&q->accessors, 1);
        return true;
    }
    if (curr_time.tv_sec - array[TS_SEC] < lim_secs) {
        (void)AFS64(&q->accessors, 1);
        return false;
    }
    if (curr_time.tv_nsec - array[TS_NSEC] > lim_nsecs) {
        (void)AFS64(&q->accessors, 1);
        return true;
    }
    if (curr_time.tv_nsec - array[TS_NSEC] < lim_nsecs) {
        (void)AFS64(&q->accessors, 1);
        return false;
    }

    (void)AFS64(&q->accessors, 1);
    return true;
}


/*
    shr_q_count -- returns count of items on queue, or -1 if it fails

*/
extern int64_t shr_q_count(
    shr_q_s *q                  // pointer to queue struct -- not NULL
)   {
    if (q == NULL) {
        return -1;
    }
    (void)AFA64(&q->accessors, 1);
    int sval = -1;
    (void)sem_getvalue((sem_t*)&q->current->array[READ_SEM], &sval);
    (void)AFS64(&q->accessors, 1);
    return sval;
}


/*
    shr_q_level -- sets value for queue depth level event generation

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL, or level not greater than 0

*/
extern sh_status_e shr_q_level(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    uint32_t level              // level at which to generate level event
)   {
    if (q == NULL || level <= 0) {
        return SH_ERR_ARG;
    }
    (void)AFA64(&q->accessors, 1);

    extent_s *extent = q->current;
    int64_t *array = extent->array;
    int64_t prev = array[LEVEL];
    CAS(&array[LEVEL], prev, level);

    (void)AFS64(&q->accessors, 1);
    return SH_OK;
}


/*==============================================================================

    unit tests

==============================================================================*/


#ifdef TESTMAIN

int32_t adds;
int32_t events;

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
    int64_t first = view.slot;
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

static void test_free_data_array4(int64_t *array)
{
    sh_status_e status;
    int64_t slot[4];
    shr_q_s *q = NULL;
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
    status = free_data_slots(q, slot[0], array[0]);
    assert(status == SH_OK);
    status = free_data_slots(q, slot[1], array[1]);
    assert(status == SH_OK);
    status = free_data_slots(q, slot[2], array[2]);
    assert(status == SH_OK);
    status = free_data_slots(q, slot[3], array[3]);
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
    int64_t test1[4] = {5, 6, 7, 8};
    test_free_data_array4(test1);
    int64_t test2[4] = {8, 7, 6, 5};
    test_free_data_array4(test2);
    int64_t test3[4] = {8, 6, 5, 7};
    test_free_data_array4(test3);
    int64_t test4[4] = {8, 5, 7, 6};
    test_free_data_array4(test4);
    int64_t test5[4] = {5, 8, 6, 7};
    test_free_data_array4(test5);
}

static void test_large_data_allocation(void)
{
    sh_status_e status;
    int64_t big_slot = 0;
    int64_t bigger_slot = 0;
    int64_t slot = 0;
    shr_q_s *q = NULL;
    status = shr_q_create(&q, "testq", 1, SQ_READWRITE);
    assert(status == SH_OK);
    view_s view = alloc_data_slots(q, 4096 >> 3);
    big_slot = view.slot;
    assert(big_slot > 0);
    status = free_data_slots(q, big_slot, 4096 >> 3);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 8192 >> 3);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    status = free_data_slots(q, bigger_slot, 8192 >> 3);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 4096 >> 3);
    slot = view.slot;
    assert(slot > 0);
    assert(big_slot == slot);
    status = free_data_slots(q, big_slot, 4096 >> 3);
    assert(status == SH_OK);
    view = alloc_data_slots(q, 8192 >> 3);
    slot = view.slot;
    assert(slot > 0);
    assert(bigger_slot == slot);
    view = alloc_data_slots(q, 4096 >> 3);
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
    int64_t length = 4;
    int64_t size = length;
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
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
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
    assert(shr_q_listen(q, SIGUSR1) == SH_OK);
    assert(shr_q_monitor(q, SIGUSR2) == SH_OK);
    assert(shr_q_add(q, "test", 4) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_INIT);
    assert(shr_q_add(q, "test", 4) == SH_ERR_LIMIT);
    assert(shr_q_event(q) == SQ_EVNT_DEPTH);
    assert(shr_q_count(q) == 1);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 4);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test", item.length) == 0);
    assert(shr_q_count(q) == 0);
    assert(shr_q_add(q, "test1", 5) == SH_OK);
    assert(shr_q_count(q) == 1);
    assert(shr_q_event(q) == SQ_EVNT_NONE);
    item = shr_q_remove(q, &item.buffer, &item.buf_size);
    assert(item.status == SH_OK);
    assert(item.buffer != NULL);
    assert(item.buf_size > 0);
    assert(item.length == 5);
    assert(item.value != NULL);
    assert(memcmp(item.value, "test1", item.length) == 0);
    assert(shr_q_count(q) == 0);
    free(item.buffer);
    assert(events == 2);
    assert(adds == 2);
    status = shr_q_destroy(&q);
    assert(status == SH_OK);
    assert(q == NULL);
}

int main(void)
{
    set_signal_handlers();

    /*
        Test functions
    */
    test_create_error_paths();
    test_create_namedq();
    test_alloc_node_slots();
    test_monitor();
    test_listen();
    test_free_data_slots();
    test_large_data_allocation();
    test_add_errors();

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
    test_empty_queue();
    test_single_item_queue();

    return 0;
}

#endif // TESTMAIN

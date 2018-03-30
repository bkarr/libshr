/*
The MIT License (MIT)

Copyright (c) 2018 Bryan Karr

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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "shared_int.h"


#ifdef __x86_64__

typedef int32_t halfword;

#else

typedef int16_t halfword;

#endif


// define useful integer constants (mostly sizes and offsets)
enum shr_int_constants
{
  
    IDX_SIZE = 4,           // index node slot count

};


/*
    reference to critbit trie node
*/
struct idx_ref
{

    long next;

    union {

        struct {

            int8_t  flag;
            uint8_t bits;
            uint8_t spares[ sizeof(halfword) - 2 ];
            halfword byte;

        };

        long diff;

    };

};


/*
    internal node of critbit trie index
*/
struct idx_node
{

    idx_ref_s child[ 2 ];

};


/*
    leaf node of critbit trie index
*/
struct idx_leaf
{

    union
    {

        uint8_t key[ sizeof(long) ];
        long count;

    };

    long pad;
    long allocs;
    long allocs_count;

};


// static null value for use in CAS
static void *null = NULL;


/*
    convert_to_status -- converts errno value to sh_status_e value

    returns sh_status_e:

    SH_ERR_ARG      invalid argument
    SH_ERR_ACCESS   permission denied or operation not permitted
    SH_ERR_EXIST    no such file or file exists
    SH_ERR_SYS      input/output error or too many open files
    SH_ERR_NOMEM    not enough memory
    SH_ERR_PATH     problem with name too long, or with path
    SH_ERR_STATE    errno value not recognized
*/
extern sh_status_e convert_to_status(

    int err                 // errno value

)   {

    switch( err ) {

        case EINVAL :

            return SH_ERR_ARG;

        case EPERM :
        case EACCES :

            return SH_ERR_ACCESS;

        case EEXIST :
        case ENOENT :

            return SH_ERR_EXIST;

        case ENOMEM :

            return SH_ERR_NOMEM;

        case EBADF :
        case ELOOP :
        case ENOTDIR :
        case ENAMETOOLONG :

            return SH_ERR_PATH;

        case ENFILE:
        case EMFILE:
        case EIO :

            return SH_ERR_SYS;

        default :

            return SH_ERR_STATE;
    }
}


/*
    validate_name -- insure name is string of valid length

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_PATH     if name is invalid

*/
extern sh_status_e validate_name(

    char const * const name

)   {

    if ( name == NULL ) {

        return SH_ERR_PATH;

    }

    size_t len = strlen( name );
    
    if ( len == 0 ) {

        return SH_ERR_PATH;

    }

    if ( len > PATH_MAX ) {

        return SH_ERR_PATH;

    }

    return SH_OK;
}


/*
    build_file_path -- builds path to shared memory file in buffer

    effects:

    buffer is cleared for length, and the name of the shared memory
    file is appended to shared memory directory in the buffer
*/
static void build_file_path(

    char const * const name,        // name string of shared memory file
    char *buffer,                   // buffer
    int size                        // buffer size

)   {
    
    // initialize buffer
    memset( buffer, 0, size );

    // copy shared memory directory
    memcpy( buffer, SHR_OBJ_DIR, sizeof(SHR_OBJ_DIR) );

    // index to end of data in buffer
    int index = sizeof(SHR_OBJ_DIR) - 1;

    // does name start with '/'?
    if ( name[ 0 ] == '/' ) {

        // backup in buffer
        index--;

    }

    // copy name into buffer
    memcpy( &buffer[ index ], name, strlen( name ) );
}


/*
    validate_existence -- validate that shared memory file does exist

    effects:

    if size pointer is not NULL, the size field will be zero on error,
    otherwise, it will contain the size of the existing shared memory file

    returns sh_status_e:

    SH_OK           shared memory file exists
    SH_ERR_ARG      invalid argument
    SH_ERR_ACCESS   permission denied or operation not permitted
    SH_ERR_EXIST    no such file or file exists
    SH_ERR_SYS      input/output error
    SH_ERR_NOMEM    not enough memory
    SH_ERR_PATH     problem with name too long, or with path
    SH_ERR_STATE    size != NULL and file has a length, or length is not page 
                    size multiple 
*/
extern sh_status_e validate_existence(

    char const * const name,        // name string of shared memory file
    size_t *size                    // pointer to size field -- possibly NULL

)   {

    if ( size ) {

        *size = 0;

    }

    if ( name == NULL ) {

        return SH_ERR_ARG;

    }

    // build file path
    int bsize = sizeof(SHR_OBJ_DIR) + strlen( name );
    char nm_buffer[ bsize ];
    build_file_path( name, &nm_buffer[ 0 ], bsize );

    // check file status
    struct stat statbuf;
    int rc = stat( &nm_buffer[ 0 ], &statbuf );
    if ( rc < 0 ) {

        // error performing stat
        return convert_to_status( errno );

    }

    if ( size == NULL || !S_ISREG( statbuf.st_mode ) ) {

        return SH_ERR_STATE;

    }
    
    *size = statbuf.st_size;
    if ( ( *size < PAGE_SIZE ) || ( *size % PAGE_SIZE != 0 ) ) {

        return SH_ERR_STATE;

    }

    return SH_OK;
}

/*
    init_base_struct -- allocate and initialize the sh_base_s struct

    effects:

    if allocation and initialization succeeds, *base with have a valid
    pointer to struct, otherwise, on error *base will be NULL

    returns sh_status_e:

    SH_OK           successful allocation/initialization
    SH_ERR_NOMEM    not enough memory
*/
static sh_status_e init_base_struct(

    shr_base_s **base,      // address of base struct pointer -- not NULL
    size_t size,            // size of structure to allocate
    char const * const name,// name of base as a null terminated string -- not NULL
    int prot,               // protection indicators
    int flags               // sharing flags

)   {
    
    // allocate base structure
    *base = calloc( 1, size );
    if ( *base == NULL ) {

        return SH_ERR_NOMEM;

    }

    // initialize base structure
    (*base)->name = strdup( name );
    if ( (*base)->name == NULL ) {

        free( *base );
        *base = NULL;
        return SH_ERR_NOMEM;

    }

    (*base)->prot = prot;
    (*base)->flags = flags;
    return SH_OK;
}

/*
    allocate_shared_memory -- creates and sets initial size of shared memory object

    effects:

    on success, the shared memory object will exist and be set to the specified
    size, otherwise, no shared memory object will exist

    returns sh_status_e:

    SH_OK           successful open and sizing
    SH_ERR_PATH     invalid path name
    SH_ERR_EXIST    shared object already exists
*/
static sh_status_e allocate_shared_memory(

    shr_base_s *base,       // address of base struct pointer -- not NULL
    char const * const name,// name of base as a null terminated string -- not NULL
    long size               // requested size for shared memory object

)   {

    // create initial shared memory object
    base->fd = shm_open( name, O_RDWR | O_CREAT | O_EXCL, FILE_MODE );
    if ( base->fd < 0 ) {

        if ( errno == EINVAL ) {

            return SH_ERR_PATH;

        }

        return convert_to_status( errno );
    }

    // set initial size
    int rc = ftruncate( base->fd, size );
    if ( rc < 0 ) {

        shm_unlink( name );
        base->fd = -1;
        return convert_to_status( errno );

    }

    return SH_OK;
}

/*
    create_extent -- creates new extent with mmapped array of specified size

    effects:

    *current will contain new extent instance with current->array pointing
    at mmapped memory array, otherwise, on error extent will not be allocated

    returns sh_status_e:

    SH_OK           successful extent mapping
    SH_ERR_NOMEM    not enough memory

*/
static sh_status_e create_extent(

    extent_s **current,     // address of extent pointer
    long slots,             // slots in extent
    int fd,                 // file descriptor
    int prot,               // protection indicators
    int flags               // sharing flags

)   {

    *current = calloc( 1, sizeof(extent_s) );
    if ( *current == NULL ) {

        return SH_ERR_NOMEM;

    }

    (*current)->size = slots << SZ_SHIFT;
    (*current)->slots = slots;

    (*current)->array = mmap( 0, (*current)->size, prot, flags, fd, 0 );
    if ( (*current)->array == (void*) -1 ) {

        free( *current );
        return convert_to_status( errno );

    }

    return SH_OK;
}


/*
    create_base_object -- creates and initializes base shared memory object

    effects:

    creates base structure with an extent that maps to an intitial allocation
    of shared memory

    returns sh_status_e:
    SH_OK           successful allocation/initialization
    SH_ERR_NOMEM    not enough memory
    SH_ERR_PATH     invalid path name
    SH_ERR_EXIST    shared object already exists

*/
extern sh_status_e create_base_object(

    shr_base_s **base,      // address of base struct pointer -- not NULL
    size_t size,            // size of structure to allocate
    char const * const name,// name of base as a null terminated string -- not NULL
    char const * const tag, // tag to initialize base shared memory structure
    int tag_len,            // length of tag
    long version            // version for memory layout

)   {

    if ( base == NULL || size < sizeof(shr_base_s) || name == NULL ||
         tag == NULL || tag_len <= 0 ) {

        return SH_ERR_ARG;

    }

    sh_status_e status = init_base_struct( base, size, name,
                                           PROT_READ | PROT_WRITE, MAP_SHARED );
    if ( status != SH_OK ) {

        return status;

    }

    status = allocate_shared_memory( *base, name, PAGE_SIZE );
    if ( status != SH_OK ) {

        free( *base );
        *base = NULL;
        return status;

    }

    status = create_extent( &(*base)->current, PAGE_SIZE >> SZ_SHIFT, (*base)->fd,
                            (*base)->prot, (*base)->flags );
    if ( status != SH_OK ) {

        free( *base );
        *base = NULL;
        return status;

    }

    (*base)->prev = (*base)->current;

    // initialize base shared memory object
    (*base)->current->array[ SIZE ] = (*base)->current->slots;
    (*base)->current->array[ EXPAND_SIZE ] = (*base)->current->size;
    (*base)->current->array[ DATA_ALLOC ] = BASE;
    (*base)->current->array[ VERSION ] = version;
    memcpy(&(*base)->current->array[ TAG ], tag, tag_len);

    return SH_OK;
}


/*
    init_data_allocator -- initializes base structures needed for data
    allocation
*/
extern void init_data_allocator(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long start          // start location for data allocations

)   {

    long *array = base->current->array;
    array[ DATA_ALLOC ] = start;
    view_s view = alloc_new_data( base, IDX_SIZE );
    array[ FREE_HEAD ] = view.slot;
    array[ FREE_HD_CNT ] = AFA( &array[ ID_CNTR ], 1 );
    array[ FREE_TAIL ] = view.slot;
    array[ FREE_TL_CNT ] = array[ FREE_HD_CNT ];
    array[ array[ FREE_HEAD ] ] = array[ FREE_TAIL ];
    array[ array[ FREE_HEAD ] + 1]  = array[ FREE_TL_CNT ];
}


/*
    resize_extent -- resize current mapped extent to match shared memory
    object size

    effects:

    if the underlying shared memory object has been expanded, a new extent
    is created and appended as the current extent at the end of linked list
    of recent extents

    returns view_s where view.status:

    SH_OK           if view reflects latest state of current extent
    SH_ERR_NOMEM    if not enough memory to resize extent to match shared memory
*/
extern view_s resize_extent(

    shr_base_s *base,   // pointer to base struct -- not NULL
    extent_s *extent    // pointer to working extent -- not NULL

)   {

    view_s view = { .status = SH_OK, .slot =  0, .extent = base->current };

    // did another thread change current extent?
    if ( extent != view.extent ) {

        return view;

    }

    // did another process change size of shared object?
    long *array = view.extent->array;
    if ( extent->slots == array[ SIZE ] ) {

        // no, return current view
        return view;

    }

    // allocate next extent
    extent_s *next = NULL;
    view.status = create_extent( &next, array[ SIZE ], base->fd, base->prot,
                                 base->flags );
    if ( view.status != SH_OK ) {

        return view;

    }

    // update current extent
    extent_s *tail = view.extent;

    if ( CAS( (long*) &tail->next, (long*) &null, (long) next ) ) {

        CAS( (long*) &base->current, (long*) &tail, (long) next );

    } else {

        CAS( (long*) &base->current, (long*) &tail, (long) tail->next );
        munmap( next->array, next->size );
        free( next );

    }

    view.extent = base->current;
    return view;
}


/*
    calculate_realloc_size -- calculate page size needed based on
    requested number of slots

    returns new page aligned size
*/
static long calculate_realloc_size(

    extent_s *extent,   // pointer to current extent -- not NULL
    long slots          // number of slots to allocate

)   {

    // divide by page size
    long current_pages = extent->size >> 12;    
    long needed_pages = ( (slots << SZ_SHIFT) >> 12 ) + 1;
    return ( current_pages + needed_pages ) * PAGE_SIZE;
}


/*
    expand -- expand the shared memory object without locking

    returns view_s where view.status:

    SH_OK           if view reflects latest state of current extent
    SH_ERR_NOMEM    if not enough memory to resize extent to match shared memory
*/
extern view_s expand(

    shr_base_s *base,   // pointer to base struct -- not NULL
    extent_s *extent,   // pointer to current extent -- not NULL
    long slots          // number of slots to allocate

)   {

    assert(base != NULL);
    assert(extent != NULL);
    assert(slots > 0);

    view_s view = { .status = SH_OK, .extent = extent };

    if ( extent != base->current ) {

        view.extent = base->current;
        return view;

    }

    atomictype *array = (atomictype*) extent->array;
    if ( extent->slots != array[ SIZE ] ) {

        return resize_extent( (shr_base_s*) base, extent );

    }

    long size = calculate_realloc_size( extent, slots );
    long prev = array[ SIZE ] << SZ_SHIFT;

    // attempt to update expansion size
    if ( size > prev ) {

        CAS( &array[ EXPAND_SIZE ], &prev, size );

    }

    // attempt to extend shared memory
    while ( ftruncate( base->fd, array[ EXPAND_SIZE ] ) < 0 ) {

        if ( errno != EINTR ) {

            view.status = SH_ERR_NOMEM;
            return view;

        }

    }

    // attempt to update size with reallocated value
    prev >>= SZ_SHIFT;
    CAS( &array[ SIZE ], &prev, size >> SZ_SHIFT );

    if ( extent->slots != array[ SIZE ] ) {

        view = resize_extent( (shr_base_s*) base, extent );

    }

    return view;
}


/*
    insure_in_range -- validates that slot is within the current extent

    effects:

    updates current extent if specified slot exceeds size of current extent

    returns view_s with view_s.status:

    SH_OK           slot is in current extent
    SH_ERR_NOMEM    not enough memory to map slot into extent
*/
extern view_s insure_in_range(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot           // starting slot

)   {

    assert(base != NULL);
    assert(slot > 0);

    if ( slot < base->current->slots ) {

        return (view_s) { .status = SH_OK, .slot = slot, .extent = base->current };

    }

    view_s view = resize_extent( base, base->current) ;
    if ( view.status == SH_OK ) {

        view.slot = slot;

    }

    return view;
}


/*
    insure_fit -- validates that the number of slots at start are in range
    of current extent

    effects:

    expands shared memory object if slot range at start location does not fit,
    otherwise, returns view with current extent

    returns view_s with view_s.status:

    SH_OK           slot is in current extent
    SH_ERR_NOMEM    not enough memory to map slot into extent
*/
static inline view_s insure_fit(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long start,         // starting slot
    long slots          // slot count

)   {

    assert(base != NULL);
    assert(start >= BASE);
    assert(slots > 0);

    view_s view = { .status = SH_OK, .slot = 0, .extent = base->current };
    long end = start + slots;

    while ( end >= view.extent->slots ) {

        view = expand( (shr_base_s*) base, view.extent, slots );

        if ( view.status != SH_OK ) {

            return view;

        }
    }

    view.extent = base->current;
    view.slot = start;
    return view;
}


/*
    set_flag -- set indicator bits in flag slot to true

    effect:

    loops until indicator bit are set to true in array[FLAGS]

    returns true if able to set, otherwise, false

    Note:  a false return does not mean bit was not set, only that
    the calling process was not the one to set it
*/
extern bool set_flag(

    long *array,
    long indicator

)   {

    assert(array != NULL);
    assert(indicator != 0);

    volatile long prev = (volatile long) array[ FLAGS ];

    while ( !( prev & indicator ) ) {

        if ( CAS( &array[ FLAGS ], &prev, prev | indicator ) ) {

            return true;

        }

        prev = (volatile long) array[ FLAGS ];
    }

    return false;
}


/*
    clear_flag -- clears indicator bits in flag slot to false

    effect:

    loops until indicator bits are set to false in array[FLAGS]

    returns true if able to clear, otherwise, false

    Note:  a false return does not mean bit was not cleared, only that
    the calling process was not the one to clear it
*/
extern bool clear_flag(

    long *array,
    long indicator

)   {

    assert(array != NULL);
    assert(indicator != 0);

    long mask = ~indicator;
    volatile long prev = (volatile long) array[ FLAGS ];

    while ( prev & indicator ) {

        if ( CAS( &array[ FLAGS ], &prev, prev & mask ) ) {

            return true;

        }

        prev = (volatile long) array[ FLAGS ];
    }

    return false;
}


/*
    update_buffer_size -- attempts to store the largest buffer size
    in shared memory
*/
extern void update_buffer_size(

    long *array,    // pointer to array -- not NUL
    long space,     // slots used for data
    long vec_sz     // size needed for vectors

)   {

    long total = space << SZ_SHIFT;
    total += vec_sz;
    long buff_sz = array[ BUFFER ];

    while ( total > buff_sz ) {

        if ( CAS( &array[ BUFFER ], &buff_sz, total ) ) {

            break;

        }

        buff_sz = array[ BUFFER ];
    }
}


/*
    add_end -- lock-free append memory to end of linked list

    effect:

    memory at slot is appended to last item in list and the tail
    reference is also updated to point to newly added item

*/
extern void add_end(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot,          // slot reference
    long tail           // tail slot of list

)   {

    // assert(base != NULL);
    // assert(slot >= BASE);
    // assert(tail > 0);

    atomictype * volatile array = (atomictype*) base->current->array;
    long gen = AFA( &array[ ID_CNTR ], 1 );
    array[ slot ] = slot;
    array[ slot + 1 ] = gen;
    DWORD next_after = { .low = slot, .high = gen };

    while( true ) {

        DWORD tail_before = *( (DWORD * volatile) &array[ tail ] );
        long next = tail_before.low;
        view_s view = insure_in_range( base, next );
        array = (atomictype * volatile) view.extent->array;

        if ( tail_before.low == array[ next ] ) {

            if ( DWCAS( (DWORD*) &array[ next ], &tail_before, next_after ) ) {

                DWCAS( (DWORD*) &array[ tail ], &tail_before, next_after );
                return;

            }

        } else {

            DWORD tail_after = *( (DWORD* volatile) &array[ next ] );
            DWCAS( (DWORD*) &array[ tail ], &tail_before, tail_after );

        }
    }
}


/*
    remove_front -- lock-free remove memory from front of linked list

    effect:

    memory at ref slot is removed from front of list with first two slots
    of memory zeroed out, otherwise, no change to memory

    returns slot of memory being returned if successful, otherwise, 0

*/
extern long remove_front(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long ref,           // expected slot number
    long gen,           // generation count
    long head,          // head slot of list
    long tail           // tail slot of list

)   {

    assert(base != NULL);

    volatile long * volatile array = base->current->array;
    DWORD before;
    DWORD after;

    if ( ref >= BASE && ref != array[ tail ] ) {

        view_s view = insure_in_range( base, ref );
        array = view.extent->array;
        after.low = array[ ref ];
        before.high = gen;
        after.high = before.high + 1;
        before.low = (ulong) ref;

        if ( DWCAS( (DWORD*) &array[ head ], &before, after ) ) {

            memset( (void*) &array[ ref ], 0, 2 << SZ_SHIFT );
            return ref;

        }

    }

    return 0;
}


/*
    alloc_new_data -- allocates the number of required slots by advancing the
    data allocaction counter from previously unused space in share memory

    returns view_s where view_s.slot will contain slot index to start of
    memory if successful, otherwise, slot will be 0
*/
extern view_s alloc_new_data(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slots          // number of slots to allocate

)   {

    long *array = base->current->array;
    long node_alloc = array[ DATA_ALLOC ];
    long alloc_end = node_alloc + slots;
    view_s view = insure_fit( base, node_alloc, slots );
    if ( view.status != SH_OK ) {

        return view;
    
    }

    array = view.extent->array;

    while ( view.status == SH_OK && alloc_end < array[ SIZE ] ) {

        if ( CAS( &array[ DATA_ALLOC ], &node_alloc, alloc_end ) ) {

            view.slot = node_alloc;
            array[ node_alloc ] = slots;
            return view;

        }

        view.extent = base->current;
        array = view.extent->array;
        node_alloc = array[ DATA_ALLOC ];
        alloc_end = node_alloc + slots;
        view = insure_fit( base, node_alloc, slots );
        array = view.extent->array;

    }

    return (view_s) { .status = SH_ERR_NOMEM };
}


/*
    realloc_idx_slots -- attempt to allocate previously freed idx slots
*/
static view_s realloc_idx_slots(

    shr_base_s *base     // pointer to base struct -- not NULL

)   {

    view_s view = { .status = SH_OK, .extent = base->current, .slot = 0 };
    long *array = view.extent->array;

    // attempt to remove from free index node list
    long gen = array[ FREE_HD_CNT ];
    long node_alloc = array[ FREE_HEAD ];

    while ( node_alloc != array[ FREE_TAIL ] ) {

        node_alloc = remove_front( base, node_alloc, gen, FREE_HEAD, FREE_TAIL );

        if ( node_alloc > 0 ) {

            view = insure_fit( base, node_alloc, IDX_SIZE );
            array = view.extent->array;

            if ( view.slot != 0 ) {

                memset( (idx_leaf_s*)&array[ node_alloc ], 0, IDX_SIZE << SZ_SHIFT );

            }

            break;

        }

        gen = array[ FREE_HD_CNT ];
        node_alloc = array[ FREE_HEAD ];

    }

    return view;
}


/*
    alloc_idx_slots -- allocate idx node slots
*/
extern view_s alloc_idx_slots(

    shr_base_s *base    // pointer to base struct -- not NULL

)   {

    assert(base != NULL);

    // attempt to remove from free index node list
    view_s view = realloc_idx_slots( base );
    if ( view.slot != 0 ) {

        return view;

    }

    // attempt to allocate new node from current extent
    view = alloc_new_data( base, IDX_SIZE );
    return view;
}


/*
    find_leaf -- return leaf that matches count as key
*/
static long find_leaf(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long count,         // number of slots to return
    long ref_index      // index node reference to begin search

)   {

    assert(base != NULL);
    assert(count > 0);
    assert(ref_index > 0);

    view_s view = { .status = SH_OK, .extent = base->current, .slot = 0 };
    long *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*) &array[ ref_index ];
    uint8_t *key = (uint8_t*) &count;
    long node_slot = ref_index;

    while ( ref->flag < 0 ) {

        node_slot = ref->next;
        view = insure_in_range( base, node_slot );

        if ( view.slot == 0 ) {

            return 0;

        }

        array = view.extent->array;
        idx_node_s *node = (idx_node_s*) &array[ node_slot ];
        long direction = ( 1 + ( ref->bits | key[ ref->byte ] ) ) >> 8;
        ref = &node->child[ direction ];

    }

    view = insure_in_range( base, ref->next );
    if ( view.slot == 0 ) {

        return 0;

    }

    return ref->next;
}


static sh_status_e add_idx_root(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot,          // start of slot range
    long count          // number of slots to return

)   {

    assert(base != NULL);
    assert(slot >= BASE);
    assert(count >= 2);

    view_s view = alloc_idx_slots( base );
    if ( view.slot == 0 ) {

        return SH_ERR_NOMEM;

    }

    long node = view.slot;
    long *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*) &array[ ROOT_FREE ];
    idx_leaf_s *leaf = (idx_leaf_s*) &array[ node ];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;
    array[ slot ] = 0;
    array[ slot + 1 ] = 0;
    DWORD before = { 0, 0 };
    DWORD after = { .low = node, .high = 0 };

    if ( !DWCAS( (DWORD*) ref, &before, after ) ) {

        add_end( base, node, FREE_TAIL );
        return SH_RETRY;

    }

    return SH_OK;
}


static sh_status_e add_to_leaf(

    shr_base_s *base,   // pointer to base struct -- not NULL
    idx_leaf_s *leaf,
    long slot

)   {

    assert(base != NULL);
    assert(leaf != NULL);
    assert(slot >= BASE);

    view_s view = insure_in_range( base, slot );
    if ( view.slot == 0 ) {

        return SH_ERR_NOMEM;

    }

    volatile long * volatile array = view.extent->array;
    DWORD before = { 0 };
    DWORD after = { 0 };
    after.low = slot;

    do {

        // point current memory at next allocation
        array[ slot + 1 ] = leaf->allocs_count;
        array[ slot ] = leaf->allocs;
        // init previous value
        before.high = (volatile long) array[ slot + 1 ];
        before.low = (volatile long) array[ slot ];
        // init next leaf value
        after.high = before.high + 1;
        after.low = slot;

    } while ( !DWCAS( (DWORD*) &leaf->allocs, &before, after ) ); // push down stack

    return SH_OK;
}

static void diff_key(

    idx_leaf_s *leaf,   // pointer to leaf -- not NULL
    uint8_t *key,       // pointer to key -- not NULL
    long *byte,         // byte displacement pointer -- not NULL
    uint8_t *bits       // bit difference pointer -- not NULL

)   {

    /*
        different orderings needed for endianness
    */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

    for ( *byte = 0; *byte < sizeof(long) ; (*byte)++ ) {

#else

    for ( *byte = sizeof(long) - 1; *byte >= 0 ; (*byte)-- ) {

#endif

        *bits = key[ *byte ] ^ leaf->key[ *byte ];
        if ( *bits ) {

            break;

        }

    }
}


static idx_ref_s *find_insertion_point(

    shr_base_s *base,   // pointer to base struct -- not NULL
    uint8_t *key,       // key of value being inserted
    long ref_index,     // current trie node reference index
    long byte,          // key byte index
    uint8_t bits        // key bit differential

)   {

    view_s view = insure_in_range( base, ref_index );
    long *array = view.extent->array;
    idx_ref_s *parent = (idx_ref_s*) &array[ ref_index ];

    while ( true ) {

        // check for leaf
        if ( parent->flag >= 0 ) {

            break;

        }

        // check if key differential offset is less than parent
        if ( parent->byte < byte ) {

            break;

        }

        // check if key differential bits are greater than parent
        if ( parent->byte == byte && parent->bits > bits ) {

            break;

        }

        //advance to next child node
        long direction = ( (1 + ( parent->bits | key[ parent->byte ] ) ) >> 8 );
        view = insure_in_range( base, parent->next );
        array = view.extent->array;
        idx_node_s *pnode = (idx_node_s*) &array[ parent->next ];
        parent = &pnode->child[ direction ];

    }

    return parent;
}


static view_s init_leaf(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot,          // start of slot range
    long count          // number of slots being freed

)   {

    view_s view = alloc_idx_slots( base );
    if ( view.slot == 0 ) {

        view.status = SH_ERR_NOMEM;
        return view;

    }

    long leaf_index = view.slot;
    long *array = view.extent->array;
    array[ slot ] = 0;
    array[ slot + 1 ] = 0;
    idx_leaf_s *leaf = (idx_leaf_s*) &array[ leaf_index ];
    leaf->count = count;
    leaf->allocs = slot;
    leaf->allocs_count = 1;
    return view;
}


static view_s init_node(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long leaf_index,    // index of new leaf
    long direction,     // child ref direction for new leaf
    idx_ref_s *parent

)   {

    view_s view = alloc_idx_slots( base );
    if ( view.slot == 0 ) {

        view.status = SH_ERR_NOMEM;
        return view;

    }

    long node_index = view.slot;
    long *array = view.extent->array;
    idx_node_s *node = (idx_node_s*) &array[ node_index ];
    idx_ref_s *ref = &node->child[ direction ];
    ref->next = leaf_index;
    ref = &node->child[ 1 - direction ];
    ref->next = parent->next;
    ref->diff = parent->diff;
    return view;
}


static sh_status_e insert_node(

    shr_base_s *base,   // pointer to base struct -- not NULL
    idx_ref_s *parent,  // pointer to parent reference
    long node_index,    // index of internal node
    long byte,          // key byte index
    uint8_t bits        // key bit differential

)   {

    DWORD before = { .low = parent->next, .high = parent->diff };
    DWORD after = { 0 };
    idx_ref_s *ref = (idx_ref_s*) &after;
    ref->next = node_index;
    ref->byte = byte;
    ref->bits = bits;
    ref->flag = -1;

    if ( !DWCAS( (DWORD*) parent, &before, after ) ) {

        return SH_RETRY;

    }

    return SH_OK;
}


static sh_status_e add_new_leaf(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot,          // start of slot range
    long count,         // number of slots being freed
    long ref_index,     // current trie node reference index
    long byte,          // key byte index
    uint8_t bits        // key bit differential

)   {

    // calculate bit differential mask and direction
    uint8_t *key = (uint8_t*) &count;
    bits = (uint8_t) ~( 1 << ( 31 - __builtin_clz( bits ) ) );
    long direction = ( 1 + ( bits | key[ byte ] ) ) >> 8;

    // find place to insert new node in trie
    idx_ref_s *parent = find_insertion_point(base, key, ref_index, byte, bits);

    // create and initialize new leaf
    view_s view = init_leaf( base, slot, count );
    if ( view.slot == 0 ) {

        return SH_ERR_NOMEM;

    }

    long leaf_index = view.slot;

    // create and initialize new internal node
    view = init_node( base, leaf_index, direction, parent );
    if ( view.slot == 0 ) {

        add_end( base, leaf_index, FREE_TAIL );
        return SH_ERR_NOMEM;

    }

    long node_index = view.slot;

    // insert new node in tree
    sh_status_e status = insert_node( base, parent, node_index, byte, bits );
    if ( status == SH_RETRY ) {

        // insertion failed, free trie node allocations
        add_end( base, leaf_index, FREE_TAIL );
        add_end( base, node_index, FREE_TAIL );

    }

    return status;
}


extern sh_status_e free_data_slots(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slot           // start of slot range

)   {

    sh_status_e status = SH_RETRY;

    long *array = base->current->array;
    long count = array[ slot ];

    do {

        // check if tree is empty
        if (array[ ROOT_FREE ] == 0) {

            status = add_idx_root( base, slot, count );

            if ( status != SH_RETRY ) {

                return status;

            }

        }

        long leaf = find_leaf( base, count, ROOT_FREE );
        if ( leaf == 0 ) {

            return SH_ERR_NOMEM;

        }

        // evaluate differences in keys
        long byte;
        uint8_t bits;
        diff_key( (idx_leaf_s*) &base->current->array[ leaf ], 
                  (uint8_t*) &count, &byte, &bits );
        if ( bits == 0 ) {

            // keys are equal
            return add_to_leaf( base, (idx_leaf_s*)&base->current->array[ leaf ],
                                slot );

        }

        // keys do not match, insert new internal node with new leaf
        status = add_new_leaf( base, slot, count, ROOT_FREE, byte, bits );

    } while ( status == SH_RETRY );

    return status;
}


static void stack_push(

    long *stack,
    long value

)   {

    if ( stack[ 0 ] < TSTACK_DEPTH ) {

        stack[ stack [ 0 ]++ ] = value;

    } else {

        memmove( stack + 1, stack + 2, ( TSTACK_DEPTH - 2 ) * sizeof(long) );
        stack[ TSTACK_DEPTH - 1 ] = value;

    }
}


static long stack_pop(

    long *stack

)   {

    return stack[ --stack[ 0 ] ];

}


static void stack_init(

    long *stack

)   {

    stack[ 0 ] = 1;
}


static bool stack_empty(

    long *stack

)   {

    return ( stack[ 0 ] <= 1 );
}


static long walk_trie(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long count,         // number of slots to return
    long *stack         // stack of node branches to search

)   {

    while ( !stack_empty( stack ) ) {

        long ref_index = stack_pop( stack );
        view_s view = insure_in_range( base, ref_index );

        if ( view.slot != ref_index ) {

            return 0;

        }

        long *array = view.extent->array;
        idx_ref_s *ref = (idx_ref_s*) &array[ ref_index ];

        if ( ref->flag >= 0 ) {

            view = insure_in_range( base, ref->next );

            if ( view.slot != ref->next ) {

                return 0;

            }

            array = view.extent->array;
            idx_leaf_s *leaf = (idx_leaf_s*) &array[ ref->next ];

            if ( leaf->count >= count && leaf->allocs != 0 ) {

                return ref->next;

            }

            continue;
        }

        view = insure_in_range( base, ref->next );

        if ( view.slot != ref->next ) {

            return 0;

        }

        array = view.extent->array;
        stack_push( stack, ref->next + 2 );
        stack_push( stack, ref->next );
    
    }

    return 0;
}



static long find_first_fit(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long count,         // number of slots to return
    long ref_index      // index node reference to begin search

)   {

    long stack[ TSTACK_DEPTH ] = { 0 };
    stack_init( stack );
    view_s view = { .status = SH_OK, .extent = base->current, .slot = 0 };
    long *array = view.extent->array;
    idx_ref_s *ref = (idx_ref_s*) &array[ ref_index ];
    uint8_t *key = (uint8_t*) &count;

    while ( ref->flag < 0 ) {

        long node_slot = ref->next;
        view = insure_in_range( base, node_slot );

        if ( view.slot == 0 ) {

            return 0;

        }

        array = view.extent->array;
        idx_node_s *node = (idx_node_s*) &array[ node_slot ];
        long direction = ( 1 + (ref->bits | key[ ref->byte ] ) ) >> 8;
        ref = &node->child[ direction ];

        // save branch not taken on stack
        if ( !direction ) {

            stack_push( stack, node_slot + 2 );

        }

    }

    view = insure_in_range( base, ref->next) ;
    if ( view.slot == 0 ) {

        return 0;

    }

    array = view.extent->array;
    idx_leaf_s *leaf = (idx_leaf_s*) &array[ view.slot ];

    if ( leaf->count >= count && leaf->allocs != 0 ) {

        return ref->next;

    }

    return walk_trie( base, count, stack );
}


/*
    lookup_freed_data -- looks for leaf that has first available
    allocation that is larger than requested numbers slots and
    attempts to allocate from leaf

    returns index slot of allocated memory, otherwise, 0
*/
static long lookup_freed_data(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slots          // number of slots to allocate

)   {

    DWORD before;
    DWORD after;

    long index = find_first_fit( base, slots, ROOT_FREE );
    if ( index == 0 ) {

        return 0;

    }

    view_s view = insure_in_range( base, index );
    long *array = view.extent->array;
    idx_leaf_s *leaf = (idx_leaf_s*) &array[ index ];

    do {

        before.low = leaf->allocs;
        before.high = leaf->allocs_count;

        if ( before.low == 0 ) {

            return 0;

        }

        view = insure_in_range( base, before.low );
        array = view.extent->array;
        after.low = (volatile long) array[ before.low ];
        after.high = before.high + 1;

    } while ( !DWCAS( (DWORD*) &leaf->allocs, &before, after ) );

    array[ before.low ] = leaf->count;
    return before.low;
}


/*
    realloc_data_slots -- reallocate previously released memory slots
    that are at least as large as the requested number of slots

    returns view_s where view_s.slot will contain slot index to start of
    memory if successful, otherwise, slot will be 0
*/
static view_s realloc_data_slots(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slots          // number of slots to allocate

)   {

    view_s view = { .status = SH_OK, .extent = base->current, .slot = 0 };
    long *array = view.extent->array;

    if (array[ ROOT_FREE ] != 0) {

        long alloc_end = lookup_freed_data( base, slots );

        if ( alloc_end > 0 ) {

            view = insure_fit( base, alloc_end, slots );
            array = view.extent->array;

            if ( view.slot != 0 ) {

                memset( &array[ alloc_end + 1 ], 0, ( slots - 1 ) << SZ_SHIFT );

            }

        }

    }

    return view;
}


/*
    alloc_data_slots -- attempts to allocate previously freed memory before
    allocating out of previously unallocated space

    returns view_s where view_s.slot will contain slot index to start of
    memory if successful, otherwise, slot will be 0
*/
extern view_s alloc_data_slots(

    shr_base_s *base,   // pointer to base struct -- not NULL
    long slots          // number of slots to allocate

)   {

    view_s view = realloc_data_slots( base, slots );

    if ( view.slot != 0 ) {

        return view;

    }

    // check to see if power of 2 to reduce fragmentation
    if ( __builtin_popcountl( slots ) > 1 ) {

        // round up to next power of 2
        slots = 1 << (LONG_BIT - __builtin_clzl( slots ) );

    }

    view = alloc_new_data( base, slots );
    return view;
}


extern void release_prev_extents(

    shr_base_s *base    // pointer to base struct -- not NULL

)   {

    extent_s *head = base->prev;

    while ( head != base->current ) {

        if ( base->accessors > 1 ) {

            return;

        }

        extent_s *next = head->next;
        if ( !CAS( (long*) &base->prev, (long*) &head, (long) next ) ) {

            return;

        }

        munmap( head->array, head->size );
        free( head );
        head = next;

    }
}

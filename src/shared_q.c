/*
The MIT License (MIT)

Copyright (c) 2015-2022 Bryan Karr

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

#include <shared_q.h>
#include "shared_int.h"


#ifdef __x86_64__

#define SHRQ "shrq"

#else

#define SHRQ "sq32"

#endif


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


// define useful integer constants (mostly sizes and offsets)
enum shr_q_constants
{

    QVERSION = 1,           // queue memory layout version
    NODE_SIZE = 4,          // node slot count
    EVENT_OFFSET = 2,       // offset in node for event for queued item
    VALUE_OFFSET = 3,       // offset in node for data slot for queued item
    DATA_HDR = 6,           // data header
    DATA_SLOTS = 0,         // total data slots (including header)
    TM_SEC = 1,             // offset for data timestamp seconds value
    TM_NSEC = 2,            // offset for data timestamp nanoseconds value
    TYPE = 3,               // offset of type indicator
    VEC_CNT = 4,            // offset for vector count
    DATA_LENGTH = 5,        // offset for data length

};


// define queue header slot offsets
enum shr_q_disp
{

    EVENT_TAIL = BASE,              // event queue tail
    EVENT_TL_CNT,                   // event queue tail counter
    TAIL,                           // item queue tail
    TAIL_CNT,                       // item queue tail counter
    TS_SEC,                         // timestamp of last add in seconds
    TS_NSEC,                        // timestamp of last add in nanoseconds
    LISTEN_PID,                     // arrival notification process id
    LISTEN_SIGNAL,                  // arrival notification signal
    EVENT_HEAD,                     // event queue head
    EVENT_HD_CNT,                   // event queue head counter
    HEAD,                           // item queue head
    HEAD_CNT,                       // item queue head counter
    EMPTY_SEC,                      // time q last empty in seconds
    EMPTY_NSEC,                     // time q last empty in nanoseconds
    LIMIT_SEC,                      // time limit interval in seconds
    LIMIT_NSEC,                     // time limit interval in nanoseconds
    NOTIFY_PID,                     // event notification process id
    NOTIFY_SIGNAL,                  // event notification signal
    DEQ_SEM,                        // deq semaphore
    ENQ_SEM = (DEQ_SEM + 4),        // enq semaphore
    CALL_PID = (ENQ_SEM +4),        // demand call notification process id
    CALL_SIGNAL,                    // demand call notification signal
    CALL_BLOCKS,                    // count of blocked remove calls
    CALL_UNBLOCKS,                  // count of unblocked remove calls
    TARGET_SEC,                     // target CoDel delay in seconds
    TARGET_NSEC,                    // target CoDel time limit in nanoseconds
    STACK_HEAD,                     // head of stack for adaptive LIFO
    STACK_HD_CNT,                   // head of stack counter
    LEVEL,                          // queue depth event level
    MAX_DEPTH,                      // queue max depth limit
    AVAIL,                          // next avail free slot
    HDR_END = (AVAIL + 18),         // end of queue header

};


/*
    queue structure
*/
struct shr_q
{

    BASEFIELDS;
    sq_mode_e mode;

};


/*
================================================================================

    private functions

================================================================================
*/


static sh_status_e format_as_queue(

    shr_q_s *q,             // pointer to queue struct -- not NULL
    unsigned int max_depth, // max depth allowed at which add of an item blocks
    sq_mode_e mode          // read/write mode

)   {

    init_data_allocator( (shr_base_s*)q, HDR_END );
    q->mode = mode;
    long *array = q->current->array;

    int rc = sem_init( (sem_t*)&array[ DEQ_SEM ], 1, 0 );

    if ( rc < 0 ) {

        return SH_ERR_NOSUPPORT;

    }

    if ( max_depth == 0 ) {

        max_depth = SEM_VALUE_MAX;

    }

    array[ MAX_DEPTH ] = max_depth;
    rc = sem_init( (sem_t*)&array[ ENQ_SEM ], 1, max_depth );

    if ( rc < 0 ) {

        return SH_ERR_NOSUPPORT;

    }

    view_s view = alloc_new_data( (shr_base_s*)q, NODE_SIZE );
    array[ EVENT_HEAD ] = view.slot;
    array[ EVENT_HD_CNT ] = AFA( &array[ ID_CNTR ], 1 );
    array[ EVENT_TAIL ] = view.slot;
    array[ EVENT_TL_CNT ] = array[ EVENT_HD_CNT ];
    array[ array[ EVENT_HEAD] ] = array[ EVENT_TAIL ];
    array[ array[ EVENT_HEAD ] + 1 ] = array[ EVENT_TL_CNT ];

    view = alloc_new_data( (shr_base_s*)q, NODE_SIZE );
    array[ HEAD ] = view.slot;
    array[ HEAD_CNT ] = AFA( &array[ ID_CNTR ], 1 );
    array[ TAIL ] = view.slot;
    array[ TAIL_CNT ] = array[ HEAD_CNT ];
    array[ array[ HEAD ] ] = array[ TAIL ];
    array[ array[ HEAD] + 1 ] = array[ TAIL_CNT ];

    return SH_OK;
}


static inline long calc_data_slots(

    long length

)   {

    long space = DATA_HDR;

    // calculate number of slots needed for data
    space += length >> SZ_SHIFT;

    // account for remainder
    if ( length & REM ) {

        space += 1;

    }

    return space;
}


static long copy_value(

    shr_q_s *q,         // pointer to queue struct
    void *value,        // pointer to value data
    long length,        // length of data
    sh_type_e type      // data type

)   {

    if ( q == NULL || value == NULL || length <= 0 ) {

        return 0;

    }

    struct timespec curr_time;
    clock_gettime( CLOCK_REALTIME, &curr_time );
    long space = calc_data_slots( length );
    update_buffer_size( q->current->array, space, sizeof(sq_vec_s) );
    view_s view = alloc_data_slots( (shr_base_s*)q, space );
    long current = view.slot;

    if ( current >= HDR_END ) {

        long *array = view.extent->array;
        array[ current + TM_SEC ] = curr_time.tv_sec;
        array[ current + TM_NSEC ] = curr_time.tv_nsec;
        array[ current + TYPE ] = type;
        array[ current + VEC_CNT ] = 1;
        array[ current + DATA_LENGTH ] = length;
        memcpy( &array[ current + DATA_HDR ], value, length );

    }

    return current;
}


static long calc_vector_slots(

    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 2

)   {

    long space = DATA_HDR;

    for( int i = 0; i < vcnt; i++ ) {

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

    if ( q == NULL || vector == NULL || vcnt < 2 ) {

        return -1;

    }

    struct timespec curr_time;
    clock_gettime( CLOCK_REALTIME, &curr_time );
    long space = calc_vector_slots( vector, vcnt );
    update_buffer_size( q->current->array, space, vcnt * sizeof(sq_vec_s) );
    view_s view = alloc_data_slots( (shr_base_s*)q, space );
    long current = view.slot;

    if ( current >= HDR_END ) {

        long *array = view.extent->array;
        array[ current + TM_SEC ] = curr_time.tv_sec;
        array[ current + TM_NSEC ] = curr_time.tv_nsec;
        array[ current + TYPE ] = SH_VECTOR_T;
        array[ current + VEC_CNT ] = vcnt;
        array[ current + DATA_LENGTH ] = ( space - DATA_HDR ) << SZ_SHIFT;
        long slot = current;
        slot += DATA_HDR;

        for ( int i = 0; i < vcnt; i++ ) {

            if ( vector[i].type <= 0 ||
                vector[i].len <= 0  ||
                vector[i].base == NULL ) {

                return -1;

            }

            array[ slot++ ] = vector[ i ].type;
            array[ slot++ ] = vector[ i ].len;
            memcpy( &array[ slot ], vector[ i ].base, vector[ i ].len );
            slot += vector[ i ].len >> SZ_SHIFT;

            if ( vector[ i ].len & REM ) {

                slot++;

            }
        }
    }

    return current;
}


static void signal_arrival(

    shr_q_s *q

)   {

    if ( q->current->array[ LISTEN_SIGNAL ] == 0 ||
         q->current->array[ LISTEN_PID ] == 0 ) {

        return;

    }

    int sval = -1;
    (void)sem_getvalue( (sem_t*)&q->current->array[ DEQ_SEM ], &sval );
    union sigval sv = { .sival_int = sval };

    if ( sval == 0 ) {

        (void)sigqueue(q->current->array[ LISTEN_PID ],
                       q->current->array[ LISTEN_SIGNAL ], sv);

    }

}


static void signal_event(

    shr_q_s *q

)   {

    if ( q->current->array[ NOTIFY_PID ] == 0 ||
         q->current->array[ NOTIFY_SIGNAL ] == 0 ) {

        return;

    }

    union sigval sv = { 0 };
    (void)sigqueue( q->current->array[ NOTIFY_PID ],
                    q->current->array[ NOTIFY_SIGNAL ], sv );
}


static void signal_call(

    shr_q_s *q

)   {

    if ( q->current->array[ CALL_PID ] == 0 ||
        q->current->array[ CALL_SIGNAL ] == 0 ) {

        return;

    }

    union sigval sv = { 0 };
    (void)sigqueue( q->current->array[ CALL_PID ],
                    q->current->array[ CALL_SIGNAL ], sv );
}


static inline bool is_monitored(

    long *array

)   {

    return ( array[ NOTIFY_SIGNAL ] && array[ NOTIFY_PID ] );
}


static inline bool is_call_monitored(

    long *array

)   {

    return ( array[ CALL_SIGNAL ] && array[ CALL_PID ] );
}


static inline bool is_discard_on_expire(

    long *array

)   {

    return ( array[ FLAGS ] & FLAG_DISCARD_EXPIRED );
}


static inline bool is_adaptive_lifo(

    long *array

)   {

    return ( array[ FLAGS ] & FLAG_LIFO_ON_LEVEL );
}


static inline bool is_codel_active(

    long *array

)   {

    return ( ( array[ TARGET_NSEC ] || array[ TARGET_SEC ] ) &&
             ( array[ LIMIT_NSEC ] || array[ LIMIT_SEC ] ) );
}


static long get_event_flag(

    sq_event_e event

)   {

    switch ( event ) {

        case SQ_EVNT_ALL:

            return ( FLAG_EVNT_INIT | FLAG_EVNT_LIMIT | FLAG_EVNT_EMPTY |
                    FLAG_EVNT_LEVEL | FLAG_EVNT_NONEMPTY | FLAG_EVNT_TIME );

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

    return !( array[ FLAGS ] & get_event_flag( event ) );
}


static bool add_event(

    shr_q_s *q,
    sq_event_e event

)   {

    view_s view = { .extent = q->current };

    long *array = view.extent->array;

    if ( event == SQ_EVNT_NONE || event_disabled( array, event ) ) {

        return false;

    }

    long flag = get_event_flag(event);
    long prev = array[FLAGS];

    if (!CAS( &array[ FLAGS ], &prev, prev & ~flag ) ) {

        return false;

    }

    // allocate queue node
    view = alloc_idx_slots( (shr_base_s*)q );
    if ( view.slot == 0 ) {

        return false;

    }

    array = view.extent->array;
    array[ view.slot + EVENT_OFFSET ] = event;

    // append node to end of queue
    add_end( (shr_base_s*)q, view.slot, EVENT_TAIL );
    return true;
}


static void update_empty_timestamp(

    long *array      // active q array

)   {

    struct timespec curr_time;
    clock_gettime( CLOCK_REALTIME, &curr_time );
    struct timespec last = *(struct timespec * volatile) &array[ EMPTY_SEC ];
    DWORD next = { .low = curr_time.tv_sec, .high = curr_time.tv_nsec };

    while ( timespeccmp(&curr_time, &last, > ) ) {

        if ( DWCAS( (DWORD*) &array[ EMPTY_SEC ], (DWORD*) &last, next ) ) {

            break;

        }

        last = *(struct timespec * volatile) &array[ EMPTY_SEC ];
    }
}


static void lifo_add(

    shr_q_s *q,         // pointer to queue struct -- not NULL
    long slot           // slot reference

)   {

    view_s view = insure_in_range( (shr_base_s*) q, slot );
    atomictype * volatile array = (atomictype*) view.extent->array;

    DWORD stack_before;
    DWORD stack_after;

    do {

        array[ slot ] = array[ STACK_HEAD ];
        array[ slot + 1 ] = array[ STACK_HD_CNT ];
        stack_before = *( (DWORD * volatile) &array[ slot ] );
        stack_after.low = slot;
        stack_after.high = stack_before.high + 1;

    } while ( !DWCAS( (DWORD*) &array[ STACK_HEAD ], &stack_before, stack_after ) );
}


static void post_process_enq(

    shr_q_s *q,         // pointer to queue, not NULL
    long count,         // prev count on queue
    DWORD curr_time     // current item time stamp

)   {

    long *array = q->current->array;

    if ( count == 0 ) {

        // queue emptied
        update_empty_timestamp( array );

    }

    bool need_signal = false;

    if ( !( array[ FLAGS ] & FLAG_ACTIVATED ) ) {

        if ( set_flag( array, FLAG_ACTIVATED ) ) {

            need_signal |= add_event( q, SQ_EVNT_INIT );

        }

    }

    if ( count == 0 ) {

        need_signal |= add_event( q, SQ_EVNT_NONEMPTY );

    }

    if ( count == array[ MAX_DEPTH ] - 1 ) {

        need_signal |= add_event( q, SQ_EVNT_LIMIT );

    }

    if ( need_signal && is_monitored( array ) ) {

        signal_event( q );

    }

    // update last add timestamp
    struct timespec prev_time = *(struct timespec*) &array[ TS_SEC ];
    DWCAS( (DWORD*) &array[ TS_SEC ], (DWORD*) &prev_time, curr_time );
    signal_arrival( q );
}


static inline void fifo_add(

    shr_q_s *q,         // pointer to queue, not NULL
    long slot          // slot reference

)   {

    // append node to end of queue
    add_end( (shr_base_s*) q, slot, TAIL );
}


static sh_status_e enq_data(

    shr_q_s *q,         // pointer to queue, not NULL
    long data_slot      // data to be added to queue

)   {

    long *array = q->current->array;
    DWORD curr_time = { .low = array[ data_slot + TM_SEC ],
                        .high = array[ data_slot + TM_NSEC ] };

    // allocate queue node
    view_s view = alloc_idx_slots( (shr_base_s*) q );

    if ( view.slot == 0 ) {

        free_data_slots( (shr_base_s*) q, data_slot );
        return SH_ERR_NOMEM;

    }

    long node = view.slot;
    array = view.extent->array;

    // point queue node to data slot
    array[ node + VALUE_OFFSET ] = data_slot;

    if ( is_adaptive_lifo( array ) && ( array[ COUNT ] >= array[ LEVEL ] ) ) {

        lifo_add( q, node );

    } else {

        fifo_add( q, node );
    }

    long count = AFA( &array[ COUNT ], 1 );

    post_process_enq( q, count, curr_time );

    release_prev_extents( (shr_base_s*) q );

    return SH_OK;
}


static sh_status_e enq(

    shr_q_s *q,         // pointer to queue, not NULL
    void *value,        // pointer to item, not NULL
    size_t length,      // length of item
    sh_type_e type      // data type

)   {

    if ( q == NULL || value == NULL || length <= 0 ) {

        return SH_ERR_ARG;

    }

    // allocate space and copy value
    long data_slot = copy_value( q, value, length, type );

    if ( data_slot == 0 ) {

        return SH_ERR_NOMEM;

    }

    if ( data_slot < HDR_END ) {

        return SH_ERR_STATE;

    }

    return enq_data( q, data_slot );
}


static sh_status_e enqv(

    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 2

)   {

    if ( q == NULL || vector == NULL || vcnt < 2 ) {

        return SH_ERR_ARG;

    }

    long data_slot;
    // allocate space and copy vector
    data_slot = copy_vector( q, vector, vcnt );

    if ( data_slot < 0 ) {

        return SH_ERR_ARG;

    }

    if ( data_slot == 0 ) {

        return SH_ERR_NOMEM;

    }

    if ( data_slot < HDR_END ) {

        return SH_ERR_STATE;

    }

    return enq_data( q, data_slot );
}


static long next_item(

    shr_q_s *q,          // pointer to queue
    long slot

)   {

    view_s view = insure_in_range( (shr_base_s*) q, slot );

    if ( view.slot == 0 ) {

        return 0;

    }

    long *array = view.extent->array;
    long next = array[ slot ];

    if ( next == 0 ) {

        return 0;

    }

    view = insure_in_range( (shr_base_s*) q, next + VALUE_OFFSET );

    if ( view.slot == 0 ) {

        return 0;

    }

    array = view.extent->array;

    if ( next < HDR_END ) {

        return 0;

    }

    return array[next + VALUE_OFFSET];
}


static bool item_exceeds_limit(

    shr_q_s *q,                     // pointer to queue
    long item_slot,                 // array index for item
    struct timespec *timelimit,     // expiration timelimit
    struct timespec *curr_time      // current time

)   {

    if ( q == NULL || item_slot < HDR_END || timelimit == NULL ) {

        return false;

    }

    if ( timelimit->tv_sec == 0 && timelimit->tv_nsec == 0 ) {

        return false;

    }

    view_s view = insure_in_range( (shr_base_s*) q, item_slot );
    if ( view.slot != item_slot ) {

        return false;

    }

    long *array = view.extent->array;
    struct timespec diff = { 0, 0 };
    struct timespec *item = (struct timespec *) &array[ item_slot + TM_SEC ];
    timespecsub( curr_time, item, &diff );
    return timespeccmp( &diff, timelimit, > );
}

static bool item_exceeds_delay(

    shr_q_s *q,                     // pointer to queue
    long item_slot,                 // array index for item
    long *array                     // array to access

)   {

    if ( q == NULL || item_slot < HDR_END || array == NULL ) {

        return false;

    }

    struct timespec current;
    clock_gettime( CLOCK_REALTIME, &current );
    if ( is_codel_active( array ) ) {

        struct timespec intrvl = { 0 };
        struct timespec last = *(struct timespec*) &array[ EMPTY_SEC ];

        if ( last.tv_sec == 0 ) {

            return item_exceeds_limit(q, item_slot,
                                      (struct timespec*) &array[ LIMIT_SEC ],
                                      &current );
        }

        timespecsub( &current, (struct timespec*) &array[ LIMIT_SEC ], &intrvl );

        if ( timespeccmp( &last, &intrvl, < ) ) {

            return item_exceeds_limit(q, item_slot,
                                      (struct timespec*) &array[ TARGET_SEC ],
                                      &current );
        }
    }

    return item_exceeds_limit( q, item_slot,
                               (struct timespec*) &array[ LIMIT_SEC ],
                               &current );
}


static void clear_empty_timestamp(

    long *array      // active q array

)   {

    volatile struct timespec last =
        *(struct timespec * volatile) &array[ EMPTY_SEC ];

    DWORD next = { .high = 0, .low = 0 };

    while ( !DWCAS( (DWORD*) &array[ EMPTY_SEC ], (DWORD*) &last, next ) ) {

        last = *(struct timespec * volatile) &array[ EMPTY_SEC ];

    }
}


static sh_status_e resize_buffer(

    long *array,        // pointer to queue array
    long data_slot,     // data item index
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size,  // pointer to length of buffer if buffer present
    long size           // required size of data

)   {

    long total = size + array[ data_slot + VEC_CNT ] * sizeof(sq_vec_s);

    if ( *buffer && *buff_size < total ) {

        free( *buffer );
        *buffer = NULL;
        *buff_size = 0;

    }

    if ( *buffer == NULL ) {

        *buffer = malloc( total );
        *buff_size = total;

        if ( *buffer == NULL ) {

            *buff_size = 0;
            return SH_ERR_NOMEM;

        }
    }

    return SH_OK;
}


static void initialize_item_vector(

    sq_item_s *item     // pointer to item -- not NULL

)   {

    uint8_t *current = (uint8_t*) item->value;

    for ( int i = 0; i < item->vcount; i++ ) {

        item->vector[ i ].type = (sh_type_e) *(long*) current;
        current += sizeof(long);
        item->vector[ i ].len = *(long*) current;
        current += sizeof(long);
        item->vector[ i ].base = current;

        if ( item->vector[ i ].len <= sizeof(long) ) {

            current += sizeof(long);

        } else {

            current += ( item->vector[ i ].len >> SZ_SHIFT ) << SZ_SHIFT;

            if ( item->vector[ i ].len & REM ) {

                current += sizeof(long);

            }
        }
    }
}


static void copy_to_buffer(

    long *array,        // pointer to queue array -- not NULL
    long data_slot,     // data item index
    sq_item_s *item,    // pointer to item -- not NULL
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size   // pointer to length of buffer if buffer present

)   {

    long size = ( array[ data_slot + DATA_SLOTS ] << SZ_SHIFT ) - sizeof(long);
    sh_status_e status = resize_buffer( array, data_slot, buffer, buff_size, size );

    if ( status != SH_OK ) {

        item->status = SH_ERR_NOMEM;
        return;

    }

    memcpy( *buffer, &array[ data_slot + 1 ], size );
    item->buffer = *buffer;
    item->buf_size = size;
    item->type = array[ data_slot + TYPE ];
    item->length = array[ data_slot + DATA_LENGTH ];
    item->timestamp = *buffer;
    item->value = (uint8_t*) *buffer + ( ( DATA_HDR - 1 ) * sizeof(long) );
    item->vcount = array[ data_slot + VEC_CNT ];
    item->vector = (sq_vec_s*) ( (uint8_t*) *buffer + size );

    if ( item->vcount == 1 ) {

        item->vector[ 0 ].type = (sh_type_e) array[ data_slot + TYPE ];
        item->vector[ 0 ].len = item->length;
        item->vector[ 0 ].base = item->value;

    } else {

        initialize_item_vector( item );
    }
}


static long remove_top(

    shr_q_s *q,         // pointer to queue struct -- not NULL
    long top,           // expected slot number -- 0 if no interest
    long gen            // generation count

)   {

    view_s view = { .status = SH_OK, .extent = q->current, .slot = 0 };
    volatile long * volatile array = view.extent->array;
    DWORD before;
    DWORD after;

    if ( top >= HDR_END && top == array[ STACK_HEAD ] &&
         gen == array[STACK_HD_CNT] ) {

        view = insure_in_range( (shr_base_s*) q, top );
        array = view.extent->array;
        after.low = array[ top ];
        before.high = gen;
        after.high = before.high + 1;
        before.low = (ulong) top;

        if ( DWCAS( (DWORD*) &array[ STACK_HEAD ], &before, after ) ) {

            memset( (void*) &array[ top ], 0, 2 << SZ_SHIFT );
            return top;

        }
    }

    return 0;
}


static long lifo_remove(

    shr_q_s *q          // pointer to queue

)   {

    long *array = q->current->array;
    long gen = array[ STACK_HD_CNT ];
    long top = array[ STACK_HEAD ];

    view_s view = insure_in_range( (shr_base_s*) q, top );
    array = view.extent->array;
    long data_slot = array[ top + VALUE_OFFSET ];

    if ( data_slot == 0 ) {

        return 0;   // try again

    }

    if ( remove_top( q, top, gen ) == 0) {

        return 0;   // try again

    }

    // free queue node
    add_end( (shr_base_s*) q, top, FREE_TAIL );
    return data_slot;
}


static long fifo_remove(

    shr_q_s *q          // pointer to queue

)   {

    long *array = q->current->array;
    long gen = array[ HEAD_CNT ];
    long head = array[ HEAD ];

    if ( head == array[ TAIL ] ) {

        return 0;   // try again

    }

    view_s view = insure_in_range( (shr_base_s*) q, head );
    array = view.extent->array;
    long data_slot = next_item( q, head );

    if ( data_slot == 0 ) {

        return 0;   // try again

    }

    if ( remove_front( (shr_base_s*) q, head, gen, HEAD, TAIL ) == 0 ) {

        return 0;   // try again

    }

    // free queue node
    add_end( (shr_base_s*) q, head, FREE_TAIL );
    return data_slot;
}


static bool safely_copy_data(

    shr_q_s *q,         // pointer to queue
    long data_slot,     // array index of data
    sq_item_s *item,    // item pointer
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size   // pointer to length of buffer if buffer present

)   {

    // insure data is accessible
    view_s view = insure_in_range( (shr_base_s*) q, data_slot );
    if ( view.slot == 0 ) {

        return false;

    }

    long *array = view.extent->array;
    long end_slot = data_slot + array[ data_slot + DATA_SLOTS ] - 1;
    view = insure_in_range( (shr_base_s*) q, end_slot );

    if ( view.slot == 0 ) {

        return false;

    }

    array = view.extent->array;

    copy_to_buffer( array, data_slot, item, buffer, buff_size );
    return true;
}


static void post_process_deq(

    shr_q_s *q,         // pointer to queue
    long data_slot,     // array index of data
    sq_item_s *item     // item pointer

)   {

    view_s view = { .extent = q->current };
    long *array = view.extent->array;
    long count = AFS(&array[ COUNT ], 1 );

    if ( is_codel_active( array ) && count == 1 ) {

        clear_empty_timestamp( array );

    }

    bool expired = is_discard_on_expire( array ) &&
                   item_exceeds_delay( q, data_slot, array );
    bool need_signal = false;

    if ( count == 1 ) {

        need_signal |= add_event( q, SQ_EVNT_EMPTY );

    }

    if ( expired ) {

        need_signal |= add_event( q, SQ_EVNT_TIME );

    }

    if ( need_signal && is_monitored( array ) ) {

        signal_event( q );

    }

    if ( expired && is_discard_on_expire( array ) ) {

        memset( item, 0, sizeof(sq_item_s) );
        free_data_slots( (shr_base_s*) q, data_slot );
        item->status = SH_ERR_EXIST;

    } else {

        free_data_slots( (shr_base_s*) q, data_slot );
        item->status = SH_OK;

    }
}


static sq_item_s deq(

    shr_q_s *q,         // pointer to queue
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size   // pointer to length of buffer if buffer present

)   {

    view_s view = { .extent = q->current };
    long *array = view.extent->array;
    sq_item_s item = { .status = SH_ERR_EMPTY };
    long data_slot = 0;

    while ( data_slot == 0 ) {

        if ( array[ STACK_HEAD ] == 0 ) {

            long head = array[ HEAD ];
            if (head == array[ TAIL ] ) {

                release_prev_extents( (shr_base_s*) q );
                return item;    // queue empty

            }

            data_slot = fifo_remove( q );

        } else {

            data_slot = lifo_remove( q );

        }
    }

    if ( safely_copy_data( q, data_slot, &item, buffer, buff_size ) ) {

        post_process_deq( q, data_slot, &item );

    }

    release_prev_extents( (shr_base_s*) q );
    return item;
}


static sq_event_e next_event(

    shr_q_s *q,          // pointer to queue
    long slot

)   {

    view_s view = insure_in_range( (shr_base_s*) q, slot );
    if ( view.slot == 0 ) {

        return SQ_EVNT_NONE;

    }

    long *array = view.extent->array;
    long next = array[ slot ];

    view = insure_in_range( (shr_base_s*) q, next );
    if ( view.slot == 0 ) {

        return SQ_EVNT_NONE;

    }

    array = view.extent->array;
    return array [ next + EVENT_OFFSET ];
}


static void check_for_level_event(

    shr_q_s *q          // pointer to queue

)   {

    view_s view = { .extent = q->current };
    long *array = view.extent->array;

    long level = array[ LEVEL ];
    if ( level <= 0 ) {

        return;

    }

    if ( event_disabled( array, SQ_EVNT_LEVEL ) )
    {
        return;
    }

    if ( array[ COUNT ] >= level && add_event( q, SQ_EVNT_LEVEL ) ) {

        signal_event( q );

    }

    return;
}


static sh_status_e initialize_q_struct(

    shr_q_s **q,            // address of q struct pointer -- not NULL
    sq_mode_e mode          // read/write mode

) {

    *q = calloc( 1, sizeof(shr_q_s) );
    if ( *q == NULL ) {

        return SH_ERR_NOMEM;

    }

    (*q)->current = calloc( 1, sizeof(extent_s) );
    if ( (*q)->current == NULL ) {

        free( *q );
        *q = NULL;
        return SH_ERR_NOMEM;

    }

    (*q)->prev = (*q)->current;
    (*q)->mode = mode;

    return SH_OK;
}



static bool is_valid_queue(

    shr_q_s *q          // pointer to queue

)   {

    if ( memcmp( &q->current->array[ TAG ], SHRQ, sizeof(SHRQ) - 1 ) != 0 ) {

        return false;

    }

    if ( q->current->array[ VERSION ] != QVERSION ) {

        return false;

    }

    return true;
}


static sh_status_e release_semaphores(

    shr_q_s **q         // address of q struct pointer -- not NULL

)   {

    int rc = sem_destroy( (sem_t*) &(*q)->current->array[ DEQ_SEM ] );
    if ( rc < 0 ) {

        return SH_ERR_SYS;

    }

    rc = sem_destroy( (sem_t*) &(*q)->current->array[ ENQ_SEM ] );
    if ( rc < 0 ) {

        return SH_ERR_SYS;

    }

    return SH_OK;
}


static sh_status_e deq_gate_try(

    shr_q_s *q          // pointer to queue

)   {

    while ( sem_trywait( (sem_t*) &q->current->array[ DEQ_SEM ] ) < 0 ) {

        if ( errno == EAGAIN ) {

            if ( is_call_monitored( q->current->array ) ) {

                signal_call( q );

            }

            return SH_ERR_EMPTY;

        }

        if ( errno == EINVAL ) {

            return SH_ERR_STATE;

        }
    }

    return SH_OK;
}


static sh_status_e deq_gate_blk(

    shr_q_s *q          // pointer to queue

)   {

    (void) AFA( &q->current->array[CALL_BLOCKS], 1 );

    if ( is_call_monitored( q->current->array ) ) {
        signal_call( q );
    }

    while ( sem_wait( (sem_t*) &q->current->array[ DEQ_SEM ] ) < 0 ) {

        if ( errno == EINVAL ) {

            (void) AFA( &q->current->array[ CALL_UNBLOCKS ], 1 );
            return SH_ERR_STATE;

        }
    }

    (void) AFA( &q->current->array[CALL_UNBLOCKS], 1 );
    return SH_OK;
}


static sh_status_e deq_gate_tm(

    shr_q_s *q,                 // pointer to queue
    struct timespec *timeout    // timeout value -- not NULL

)   {

    (void) AFA( &q->current->array[ CALL_BLOCKS ], 1 );

    if ( is_call_monitored( q->current->array ) ) {

        signal_call( q );

    }

    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    timespecadd( &ts, timeout, &ts );

    while ( sem_timedwait( (sem_t*) &q->current->array[ DEQ_SEM ], &ts ) < 0 ) {

        if ( errno == ETIMEDOUT ) {

            (void) AFA( &q->current->array[ CALL_UNBLOCKS ], 1 );
            return SH_ERR_EMPTY;

        }

        if ( errno == EINVAL ) {

            (void) AFA( &q->current->array[ CALL_UNBLOCKS ], 1 );
            return SH_ERR_STATE;

        }
    }

    (void) AFA( &q->current->array[ CALL_UNBLOCKS ], 1 );
    return SH_OK;
}


static sh_status_e enq_gate_try(

    shr_q_s *q          // pointer to queue

)   {

    while ( sem_trywait( (sem_t*) &q->current->array[ ENQ_SEM ] ) < 0 ) {

        if ( errno == EAGAIN ) {

            return SH_ERR_LIMIT;

        }

        if ( errno == EINVAL ) {

            return SH_ERR_STATE;

        }
    }

    return SH_OK;
}


static sh_status_e enq_gate_blk(

    shr_q_s *q          // pointer to queue

)   {

    while ( sem_wait( (sem_t*) &q->current->array[ ENQ_SEM ] ) < 0 ) {

        if ( errno == EINVAL ) {

            return SH_ERR_STATE;

        }
    }

    return SH_OK;
}


static sh_status_e enq_gate_tm(

    shr_q_s *q,                 // pointer to queue
    struct timespec *timeout    // timeout value -- not NULL

)   {

    long *array = q->current->array;
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    timespecadd( &ts, timeout, &ts );

    while ( sem_timedwait( (sem_t*) &array[ ENQ_SEM ], &ts ) < 0 ) {

        if ( errno == ETIMEDOUT ) {

            return SH_ERR_LIMIT;

        }

        if ( errno == EINVAL ) {

            return SH_ERR_STATE;

        }
    }

    return SH_OK;
}


static sh_status_e deq_release_gate(

    shr_q_s *q          // pointer to queue

)   {

    while ( sem_post( (sem_t*) &q->current->array[ DEQ_SEM ]) < 0 ) {

        if ( errno == EINVAL ) {

            return SH_ERR_STATE;

        }
    }

    return SH_OK;
}


static sh_status_e enq_release_gate(

    shr_q_s *q          // pointer to queue

)   {

    while ( sem_post( (sem_t*) &q->current->array[ ENQ_SEM ] ) < 0 ) {

        if ( errno == EINVAL ) {

            return SH_ERR_STATE;

        }
    }

    return SH_OK;
}


static inline void guard_q_memory(

    shr_q_s *q          // pointer to queue

)   {
    (void) AFA( &q->accessors, 1 );
}


static inline void unguard_q_memory(

    shr_q_s *q          // pointer to queue

)   {
    (void) AFS( &q->accessors, 1 );
}


/*
================================================================================

    public function interface

================================================================================
*/


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
    SH_ERR_NOMEM    if not enough memory to allocate
    SH_ERR_PATH     if error in queue name
    SH_ERR_SYS      if system call returns an error
*/
extern sh_status_e shr_q_create(

    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    unsigned int max_depth, // max depth allowed at which add of item is blocked
    sq_mode_e mode          // read/write mode

)   {

    if ( q == NULL || name == NULL || max_depth > SEM_VALUE_MAX ) {

        return SH_ERR_ARG;

    }

    sh_status_e status = perform_name_validations( name, NULL );
    if ( status == SH_ERR_STATE ) {

        return SH_ERR_EXIST;

    }

    if ( status != SH_ERR_EXIST ) {

        return status;

    }

    status = create_base_object( (shr_base_s**) q, sizeof(shr_q_s), name, SHRQ,
                                 sizeof(SHRQ) - 1, QVERSION );
    if ( status ) {

        return status;

    }

    status = format_as_queue( *q, max_depth, mode );
    if ( status ) {

        free( *q );
        *q = NULL;

    }

    return status;
}


/*
    shr_q_open -- open shared memory queue for modification using name

    Opens shared queue using name to mmap shared memory.  Queue must already
    exist before it is opened.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct or name is not NULL
	SH_ERR_NOMEM	failed memory allocation
    SH_ERR_ACCESS   on permissions error for queue name
    SH_ERR_EXIST    if queue does not already exist
    SH_ERR_PATH     if error in queue name
    SH_ERR_STATE    if incompatible implementation
    SH_ERR_SYS      if system call returns an error
*/
extern sh_status_e shr_q_open(

    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    sq_mode_e mode          // read/write mode

)   {

    if ( q == NULL || name == NULL ) {

        return SH_ERR_ARG;
    }


    size_t size = 0;
    sh_status_e status = perform_name_validations( name, &size );
    if ( status ) {

        return status;

    }

    status = initialize_q_struct( q, mode );
    if ( status ) {

        return status;

    }

    status = map_shared_memory( (shr_base_s**) q, name, size );
    if ( status ) {

        return status;

    }

    if ( is_valid_queue( *q ) ) {

        return SH_OK;

    }

    shr_q_close( q );
    return SH_ERR_STATE;
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

    if ( q == NULL || *q == NULL ) {

        return SH_ERR_ARG;

    }

    close_base( (shr_base_s*) *q );

    free( *q );
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

    if ( q == NULL || *q == NULL ) {

        return SH_ERR_ARG;

    }

    release_prev_extents( (shr_base_s*) *q );

    sh_status_e status = release_semaphores( q );
    if ( status ) {

        free( *q );
        *q = NULL;
        return status;

    }

    status = release_mapped_memory( (shr_base_s**) q );

    free( *q );
    *q = NULL;

    return status;
}


/*
    shr_q_monitor -- registers calling process for notification using signals
                     when events occur on shared queue

    Any non-zero signal value registers calling process for notification using
    the specified signal when an queue event occurs.  A signal value of zero
    unregisters the process if it is currently registered.  Only a single
    process can be registered for event notifications.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to queue struct is NULL, or if signal not greater
                    than or equal to zero, or signal not in valid range
    SH_ERR_STATE    if unable to add pid
*/
extern sh_status_e shr_q_monitor(

    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for event notification

)   {

    if ( q == NULL || signal < 0 ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    long pid = getpid();
    long prev = q->current->array[ NOTIFY_PID ];

    if ( signal == 0 ) {

        pid = 0;

    }


    if ( CAS( &q->current->array[ NOTIFY_PID ], &prev, pid ) ) {

        q->current->array[ NOTIFY_SIGNAL ] = signal;
        unguard_q_memory( q );
        return SH_OK;

    }

    unguard_q_memory( q );
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

    if ( q == NULL || signal < 0 ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    long pid = getpid();
    long prev = q->current->array[ LISTEN_PID ];

    if ( signal == 0 ) {

        pid = 0;

    }

    if ( CAS( &q->current->array[ LISTEN_PID ], &prev, pid ) ) {

        q->current->array[ LISTEN_SIGNAL ] = signal;
        unguard_q_memory( q );
        return SH_OK;

    }

    unguard_q_memory( q );
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

    if ( q == NULL || signal < 0 ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    long pid = getpid();
    long prev = q->current->array[ CALL_PID ];

    if ( signal == 0 ) {

        pid = 0;

    }


    if ( CAS( &q->current->array[ CALL_PID ], &prev, pid ) ) {

        q->current->array[ CALL_SIGNAL ] = signal;
        unguard_q_memory( q );
        return SH_OK;

    }

    unguard_q_memory( q );
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

    if ( q == NULL || value == NULL || length <= 0 ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_WRITE_ONLY ) ) {

        return SH_ERR_STATE;

    }

    guard_q_memory( q );

    sh_status_e status = enq_gate_try( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    status = enq( q, value, length, SH_STRM_T );

    if ( status ) {

        enq_release_gate( q );
        unguard_q_memory( q );
        return status;

    }

    status = deq_release_gate( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;
    }


    check_for_level_event( q );

    unguard_q_memory( q );
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

    if ( q == NULL || value == NULL || length <= 0 ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_WRITE_ONLY ) ) {

        return SH_ERR_STATE;

    }

    guard_q_memory( q );

    sh_status_e status = enq_gate_blk( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    status = enq( q, value, length, SH_STRM_T );

    if ( status != SH_OK ) {

        enq_release_gate( q );
        unguard_q_memory( q );
        return status;

    }

    status = deq_release_gate( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    check_for_level_event( q );

    unguard_q_memory( q );
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

    if ( q == NULL || value == NULL || length <= 0 || timeout == NULL ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_WRITE_ONLY ) ) {

        return SH_ERR_STATE;

    }

    guard_q_memory( q );

    sh_status_e status = enq_gate_tm( q, timeout );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    status = enq( q, value, length, SH_STRM_T );
    if ( status ) {

        enq_release_gate( q );
        unguard_q_memory( q );
        return status;

    }

    status = deq_release_gate( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    check_for_level_event( q );

    unguard_q_memory( q );
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

    if ( q == NULL || vector == NULL || vcnt < 1 ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_WRITE_ONLY ) ) {

        return SH_ERR_STATE;

    }

    guard_q_memory( q );

    sh_status_e status = enq_gate_try( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    if ( vcnt == 1 ) {

        status = enq( q, vector[ 0 ].base, vector[ 0 ].len, vector[ 0 ].type );

    } else {

        status = enqv( q, vector, vcnt );

    }

    if ( status ) {

        enq_release_gate( q );
        unguard_q_memory( q );
        return status;

    }

    status = deq_release_gate( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    check_for_level_event( q );

    unguard_q_memory( q );
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

    if ( q == NULL || vector == NULL || vcnt < 1 ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_WRITE_ONLY ) ) {

        return SH_ERR_STATE;

    }

    guard_q_memory( q );

    sh_status_e status = enq_gate_blk( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    if ( vcnt == 1 ) {

        status = enq( q, vector[ 0 ].base, vector[ 0 ].len, vector[ 0 ].type );

    } else {

        status = enqv( q, vector, vcnt) ;

    }

    if ( status ) {

        enq_release_gate( q );
        unguard_q_memory( q );
        return status;

    }

    status = deq_release_gate( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    check_for_level_event( q );

    unguard_q_memory( q );
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

    if ( q == NULL || vector == NULL || vcnt < 1 || timeout == NULL ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_WRITE_ONLY ) ) {

        return SH_ERR_STATE;

    }

    guard_q_memory( q );

    sh_status_e status = enq_gate_tm( q, timeout );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    if ( vcnt == 1 ) {

        status = enq( q, vector[ 0 ].base, vector[ 0 ].len, vector[ 0 ].type );

    } else {

        status = enqv( q, vector, vcnt );

    }

    if ( status != SH_OK ) {

        enq_release_gate( q );
        unguard_q_memory( q );
        return status;

    }

    status = deq_release_gate( q );
    if ( status ) {

        unguard_q_memory( q );
        return status;

    }

    check_for_level_event( q );

    unguard_q_memory( q );
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

    if ( q == NULL ||
         buffer == NULL ||
         buff_size == NULL ||
         ( *buffer != NULL && *buff_size <= 0 ) ) {

        return (sq_item_s) { .status = SH_ERR_ARG };

    }

    if ( !( q->mode & SQ_READ_ONLY ) ) {

        return (sq_item_s) { .status = SH_ERR_STATE };

    }

    sq_item_s item = { 0 };

    guard_q_memory( q );

    while ( true ) {

        item.status = deq_gate_try( q );
        if ( item.status ) {

            break;

        }

        item = deq( q, buffer, buff_size );
        if ( item.status != SH_ERR_EXIST ) {

            if ( item.status ) {

                deq_release_gate( q );

            } else {

                item.status = enq_release_gate( q );

            }

            break;

        }

        enq_release_gate( q );
    
    }

    unguard_q_memory( q );
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

    if ( q == NULL ||
         buffer == NULL ||
         buff_size == NULL ||
         ( *buffer != NULL && *buff_size <= 0 ) ) {

        return (sq_item_s) { .status = SH_ERR_ARG };

    }

    if ( !( q->mode & SQ_READ_ONLY ) ) {

        return (sq_item_s) { .status = SH_ERR_STATE };

    }

    sq_item_s item = { 0 };

    guard_q_memory( q );

    while ( true ) {

        item.status = deq_gate_blk( q );
        if ( item.status ) {

            break;

        }

        item = deq( q, buffer, buff_size );
        if ( item.status != SH_ERR_EXIST ) {

            if ( item.status ) {

                deq_release_gate( q );

            } else {

                item.status = enq_release_gate( q );

            }

            break;

        }

        enq_release_gate( q );
    
    }

    unguard_q_memory( q );
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

    if ( q == NULL ||
         buffer == NULL ||
         buff_size == NULL ||
         ( *buffer != NULL && *buff_size <= 0 ) ||
         timeout == NULL ) {

        return (sq_item_s) { .status = SH_ERR_ARG };

    }

    if ( !( q->mode & SQ_READ_ONLY ) ) {

        return (sq_item_s) { .status = SH_ERR_STATE };

    }

    sq_item_s item = { 0 };

    guard_q_memory( q );

    while ( true ) {

        item.status = deq_gate_tm( q, timeout );
        if ( item.status ) {

            break;

        }

        item = deq( q, buffer, buff_size );
        if ( item.status != SH_ERR_EXIST ) {

            if ( item.status ) {

                deq_release_gate( q );

            } else {

                item.status = enq_release_gate( q );

            }

            break;

        }

        enq_release_gate( q );

    }

    unguard_q_memory( q );
    return item;
}


/*
    shr_q_event -- returns active event or SQ_EVNT_NONE when empty

*/
extern sq_event_e shr_q_event(

    shr_q_s *q                  // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return SQ_EVNT_NONE;

    }

    guard_q_memory( q );

    extent_s *extent = q->current;
    long *array = extent->array;
    sq_event_e event = SQ_EVNT_NONE;

    long gen = array[ EVENT_HD_CNT ];
    long head = array[ EVENT_HEAD ];

    while ( head != array[ EVENT_TAIL ] ) {

        event = next_event( q, head );

        if ( remove_front( (shr_base_s*) q, head, gen, EVENT_HEAD, EVENT_TAIL ) != 0 ) {

            // free queue node
            add_end( (shr_base_s*) q, head, FREE_TAIL );
            break;

        }

        gen = array[ EVENT_HD_CNT ];
        head = array[ EVENT_HEAD ];
    }

    release_prev_extents( (shr_base_s*) q );

    unguard_q_memory( q );
    return event;
}


/*
    shr_q_explain -- return a null-terminated string explanation of status code

    returns non-NULL null-terminated string error explanation
*/
extern char *shr_q_explain(

    sh_status_e status          // status code

)   {

    return shr_explain( status );
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

    if ( q == NULL ) {

        return false;

    }

    guard_q_memory( q );

    extent_s *extent = q->current;
    long *array = extent->array;
    struct timespec curr_time;
    (void) clock_gettime( CLOCK_REALTIME, &curr_time );

    if ( curr_time.tv_sec - array[ TS_SEC ] > lim_secs ) {

        unguard_q_memory( q );
        return true;

    }

    if ( curr_time.tv_sec - array[ TS_SEC ] < lim_secs ) {

        unguard_q_memory( q );
        return false;

    }

    if ( curr_time.tv_nsec - array[ TS_NSEC ] > lim_nsecs ) {

        unguard_q_memory( q );
        return true;

    }

    if ( curr_time.tv_nsec - array[ TS_NSEC ] < lim_nsecs ) {

        unguard_q_memory( q );
        return false;

    }

    unguard_q_memory( q );
    return true;
}


/*
    shr_q_count -- returns count of items on queue, or -1 if it fails

*/
extern long shr_q_count(

    shr_q_s *q                  // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return -1;

    }

    guard_q_memory( q );

    long result = -1;
    result = q->current->array[ COUNT ];

    unguard_q_memory( q );
    return result;
}


/*
    shr_q_buffer -- returns max size needed to read items from queue

*/
extern size_t shr_q_buffer(

    shr_q_s *q                  // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return 0;

    }

    guard_q_memory( q );

    long result = -1;
    result = q->current->array[ BUFFER ];

    unguard_q_memory( q );
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

    if ( q == NULL || level <= 0 ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    extent_s *extent = q->current;
    long *array = extent->array;
    long prev = array[ LEVEL ];

    CAS( &array[ LEVEL ], &prev, level );

    unguard_q_memory( q );
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

    if ( q == NULL ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    long *array = q->current->array;
    struct timespec prev;
    DWORD next;
    next.low = seconds;
    next.high = nanoseconds;

    do {

        prev.tv_sec = (volatile time_t) array[ LIMIT_SEC ];
        prev.tv_nsec = (volatile long) array[ LIMIT_NSEC ];

    } while ( !DWCAS( (DWORD*) &array[ LIMIT_SEC ], (DWORD*) &prev, next ) );

    unguard_q_memory( q );
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

    if ( q == NULL || timelimit == NULL ) {

        return SH_ERR_ARG;

    }

    if ( !( q->mode & SQ_READ_ONLY ) ) {

        return SH_ERR_STATE;

    }

    sh_status_e status;
    struct timespec curr_time;

    guard_q_memory( q );

    while( true ) {

        status = deq_gate_try( q );

        if ( status ) {

            if ( status == SH_ERR_EMPTY ) {

                status = SH_OK;

            }

            unguard_q_memory( q );
            return status;
        }

        long *array = q->current->array;
        long gen = array[ HEAD_CNT ];
        long head = array[ HEAD ];

        if ( head == array[ TAIL ] ) {

            break;

        }

        view_s view = insure_in_range( (shr_base_s*) q, head );
        array = view.extent->array;
        long data_slot = next_item( q, head );

        if ( data_slot == 0 ) {

            break;

        }

        // insure data is accessible
        view = insure_in_range( (shr_base_s*) q, data_slot );
        if ( view.slot == 0 ) {

            break;

        }

        clock_gettime( CLOCK_REALTIME, &curr_time );

        if ( !item_exceeds_limit( q, data_slot, timelimit, &curr_time ) ) {

            break;

        }

        if ( remove_front( (shr_base_s*) q, head, gen, HEAD, TAIL ) == 0 ) {

            break;

        }

        (void) AFS( &array[COUNT], 1 );

        // free queue node
        add_end( (shr_base_s*) q, head, FREE_TAIL );
        free_data_slots( (shr_base_s*) q, data_slot );

        status = enq_release_gate( q );
        if ( status ) {

            unguard_q_memory( q );
            return status;

        }
    }

    status = deq_release_gate( q );
    unguard_q_memory( q );
    return status;
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

    if ( q == NULL || timestamp == NULL ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    if ( q->current->array[COUNT] == 0 ) {

        unguard_q_memory( q );
        return SH_ERR_EMPTY;

    }

    *timestamp = *(struct timespec *) &q->current->array[ EMPTY_SEC ];
    unguard_q_memory( q );
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

    guard_q_memory( q );

    if (flag) {

        set_flag(q->current->array, FLAG_DISCARD_EXPIRED);

    } else {

        clear_flag(q->current->array, FLAG_DISCARD_EXPIRED);

    }

    unguard_q_memory( q );
    return SH_OK;
}


/*
    shr_q_will_discard -- tests to see if queue will discard expired items

    returns true if expired items will be discarded, otherwise false
*/
extern bool shr_q_will_discard(

    shr_q_s *q                  // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return false;

    }

    guard_q_memory( q );

    bool result = is_discard_on_expire( q->current->array );

    unguard_q_memory( q );
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

    if ( q == NULL ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    if ( flag ) {

        set_flag( q->current->array, FLAG_LIFO_ON_LEVEL );

    } else {

        clear_flag( q->current->array, FLAG_LIFO_ON_LEVEL );

    }

    unguard_q_memory( q );
    return SH_OK;
}


/*
    shr_q_will_lifo -- tests to see if queue will use adaptive LIFO

    returns true if queue will use adaptive LIFO, otherwise false
*/
extern bool shr_q_will_lifo(

    shr_q_s *q                  // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return false;

    }

    guard_q_memory( q );

    bool result = is_adaptive_lifo( q->current->array );

    unguard_q_memory( q );
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

    if ( q == NULL ) {

        return SH_ERR_ARG;

    }

    long flag = get_event_flag( event );
    guard_q_memory( q );

    if ( flag ) {

        set_flag( q->current->array, flag );

    }

    unguard_q_memory( q );
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

    if ( q == NULL ) {

        return SH_ERR_ARG;

    }

    long flag = get_event_flag( event );
    guard_q_memory( q );

    if ( flag ) {

        clear_flag( q->current->array, flag );
    }

    unguard_q_memory( q );
    return SH_OK;
}


/*
    shr_q_is_subscribed -- tests a single event to see if it will be generated

    returns true if event has subscription, otherwise false
*/
extern bool shr_q_is_subscribed(

    shr_q_s *q,             // pointer to queue struct -- not NULL
    sq_event_e event        // event to disable

)   {

    if ( q == NULL || event == SQ_EVNT_NONE || event == SQ_EVNT_ALL ) {

        return false;

    }

    guard_q_memory( q );

    bool result = !event_disabled( q->current->array, event );

    unguard_q_memory( q );
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

    shr_q_s *q      // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    while ( sem_post( (sem_t*) &q->current->array[ DEQ_SEM ] ) < 0 ) {

        if ( errno == EINVAL ) {

            unguard_q_memory( q );
            return SH_ERR_STATE;

        }
    }

    unguard_q_memory( q );
    return SH_OK;
}


/*
    shr_q_call_count -- returns count of blocked remove calls, or -1 if it fails

*/
extern long shr_q_call_count(

    shr_q_s *q      // pointer to queue struct -- not NULL

)   {

    if ( q == NULL ) {

        return -1;

    }

    guard_q_memory( q );

    long unblocks = q->current->array[ CALL_UNBLOCKS ];
    long result = q->current->array[ CALL_BLOCKS]  - unblocks;

    unguard_q_memory( q );
    return result;
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

)   {

    if ( q == NULL ) {

        return SH_ERR_ARG;

    }

    guard_q_memory( q );

    long *array = q->current->array;
    struct timespec prev;
    DWORD next;
    next.low = seconds;
    next.high = nanoseconds;

    do {

        prev.tv_sec = (volatile time_t) array[ TARGET_SEC ];
        prev.tv_nsec = (volatile long) array[ TARGET_NSEC ];

    } while ( !DWCAS( (DWORD*) &array[ TARGET_SEC ], (DWORD*) &prev, next ) );

    unguard_q_memory( q );
    return shr_q_discard( q, true );
}


/*
    shr_q_is_valid -- returns true if name is a valid queue

    returns true if shared memory file is valid queue, otherwise, false
*/
extern bool shr_q_is_valid(

    char const * const name // name of q as a null terminated string -- not NULL

)   {

    size_t size = 0;
    sh_status_e status = perform_name_validations( name, &size );
    if ( status != SH_OK ) {

        return false;

    }

    int fd = shm_open( name, O_RDONLY, FILE_MODE );
    if ( fd < 0 ) {

        return false;

    }

    long *array = mmap( 0, size, PROT_READ, MAP_SHARED, fd, 0 );
    if ( array == (void*) -1 ) {

        close( fd );
        return false;

    }

    if ( memcmp( &array[ TAG ], SHRQ, sizeof(SHRQ) - 1 ) != 0 ) {

        munmap( array, size );
        close( fd );
        return false;

    }

    if ( array[ VERSION ] != QVERSION ) {

        munmap( array, size );
        close( fd );
        return false;

    }

    munmap( array, size );
    close( fd );
    return true;
}

#ifndef SHARED_SQ_H_
#define SHARED_SQ_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <shared.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct shr_q shr_q_s;

typedef enum
{
    SQ_EVNT_ALL = 0,        // not an event, used to simplify subscription
    SQ_EVNT_NONE = 0,       // non-event
    SQ_EVNT_INIT,           // first item added to queue
    SQ_EVNT_LIMIT,          // queue limit reached
    SQ_EVNT_TIME,           // max time limit reached
    SQ_EVNT_LEVEL,          // depth level reached 
    SQ_EVNT_EMPTY,          // last item on queue removed
    SQ_EVNT_NONEMPTY        // item added to empty queue
} sq_event_e;

typedef enum
{
    SQ_IMMUTABLE = 0,       // queue instance unable to modify queue contents
    SQ_READ_ONLY,           // queue instance able to remove items from queue
    SQ_WRITE_ONLY,          // queue instance able to add items to queue
    SQ_READWRITE            // queue instance can add/remove items
} sq_mode_e;


typedef struct sq_vec
{
#ifdef __x86_64__
    uint32_t _zeroes_;      // pad for alignment
    sh_type_e type;         // type of data in vector
#else
    sh_type_e type;         // type of data in vector
#endif
    size_t len;             // length of data
    void *base;             // pointer to vector data
} sq_vec_s;

typedef struct  sq_item
{
    sh_status_e status;         // returned status
    sh_type_e type;             // data type
    size_t length;              // length of data being returned
     void *value;               // pointer to data value being returned
    struct timespec *timestamp; // pointer to timestamp of add to queue
    void *buffer;               // pointer to data buffer
    size_t buf_size;            // size of buffer
    int vcount;                 // vector count
    sq_vec_s *vector;           // array of vectors
} sq_item_s;

/*==============================================================================

    public function interface

==============================================================================*/


extern sh_status_e shr_q_create(
    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    unsigned int max_depth, // max depth allowed at which add of item is blocked
    sq_mode_e mode          // read/write mode
);


extern sh_status_e shr_q_open(
    shr_q_s **q,            // address of q struct pointer -- not NULL
    char const * const name,// name of q as a null terminated string -- not NULL
    sq_mode_e mode          // read/write mode
);


extern sh_status_e shr_q_close(
    shr_q_s **q         // address of q struct pointer -- not NULL
);


extern sh_status_e shr_q_destroy(
    shr_q_s **q         // address of q struct pointer -- not NULL
);


extern sh_status_e shr_q_monitor(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for event notification
);


extern sh_status_e shr_q_listen(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for item arrival notification
);


extern sh_status_e shr_q_call(
    shr_q_s *q,         // pointer to queue struct
    int signal          // signal to use for queue empty notification
);


extern sh_status_e shr_q_add(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    void *value,        // pointer to item -- not NULL
    size_t length       // length of item -- greater than 0
);


extern sh_status_e shr_q_add_wait(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    void *value,        // pointer to item -- not NULL
    size_t length       // length of item -- greater than 0
);


extern sh_status_e shr_q_add_timedwait(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    void *value,        // pointer to item -- not NULL
    size_t length,      // length of item -- greater than 0
    struct timespec *timeout    // timeout value -- not NULL
);


extern sh_status_e shr_q_addv(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 1
);


extern sh_status_e shr_q_addv_wait(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt            // count of vector array -- must be >= 1
);


extern sh_status_e shr_q_addv_timedwait(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    sq_vec_s *vector,   // pointer to vector of items -- not NULL
    int vcnt,           // count of vector array -- must be >= 1
    struct timespec *timeout    // timeout value -- not NULL
);


extern sq_item_s shr_q_remove(
    shr_q_s *q,         // pointer to queue structure -- not NULL
    void **buffer,      // address of buffer pointer -- not NULL
    size_t *buff_size   // pointer to size of buffer -- not NULL
);


extern sq_item_s shr_q_remove_wait(
    shr_q_s *q,             // pointer to queue struct -- not NULL
    void **buffer,          // address of buffer pointer -- not NULL
    size_t *buff_size       // pointer to size of buffer -- not NULL
);


extern sq_item_s shr_q_remove_timedwait(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size,          // pointer to size of buffer -- not NULL
    struct timespec *timeout    // timeout value -- not NULL
);


extern sq_event_e shr_q_event(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern bool shr_q_exceeds_idle_time(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t lim_secs,            // time limit in seconds
    long lim_nsecs              // time limit in nanoseconds
);


extern long shr_q_count(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern size_t shr_q_buffer(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern sh_status_e shr_q_level(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    int level                   // level at which to generate level event
);


extern sh_status_e shr_q_timelimit(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t seconds,             // number of seconds till event
    long nanoseconds            // number of nanoseconds till event
);


extern sh_status_e shr_q_clean(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    struct timespec *timelimit  // timelimit value -- not NULL
);


extern sh_status_e shr_q_last_empty(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    struct timespec *timestamp  // timestamp pointer -- not NULL
);


extern sh_status_e shr_q_discard(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    bool flag                   // true will cause items to be discarded
);


extern bool shr_q_will_discard(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern sh_status_e shr_q_limit_lifo(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    bool flag                   // true will turn on adaptive LIFO behavior
);


extern bool shr_q_will_lifo(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern sh_status_e shr_q_subscribe(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_event_e event            // event to enable
);


extern sh_status_e shr_q_unsubscribe(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_event_e event            // event to disable
);


extern bool shr_q_is_subscribed(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    sq_event_e event            // event to disable
);


extern sh_status_e shr_q_prod(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern long shr_q_call_count(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


extern sh_status_e shr_q_target_delay(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t seconds,             // delay number of seconds
    long nanoseconds            // delay number of nanoseconds
);


extern bool shr_q_is_valid(
    char const * const name // name of q as a null terminated string -- not NULL
);


#ifdef __cplusplus
}

#endif


#endif // SHARED_SQ_H_

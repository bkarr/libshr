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


/*==============================================================================

    shared_q

    Implementation of a queue in linear, shared memory space that is accessible
    by multiple processes.  The implementation attempts to be lock-free as much
    as possible, with the exception of expanding the queue dynamically
    because the shared memory object cannot be resized atomically to always be
    the largest required size.

    Any unique name, with an optional '/' prefix, as a valid name for POSIX
    shared memory object can be used for the queue name.  A single process
    should be responsible for creating the queue since a race condition exists,
    but all other processes can simply open an existing queue by name after it
    is created, and can share that queue instance among threads internally.

    The shared queue allows items of arbitrary size to be added and removed.  In
    addition, the only limit to number of queues is the number of open files per
    process.  The queue size limit is the system max shared memory size and max
    file size.  The maximum limit on the number of items in queue is
    SEM_VALUE_MAX.

    A single process can request notification that an event is available for
    reading from queue, and the notified process should read events until a
    non-event is returned.

    The base structure is a shared memory mapped region that is treated as a
    linear array of 64-bit signed integers.  Embedded within the array are a
    list of available nodes, a critbit trie that tracks free data blocks, a
    lock-free queue of events, and a lock-free queue of references to data
    blocks.  The node list and critbit trie are used for internal memory
    management.

    Note:

    Assumes x86_64 architecture with little endian integers and cmpxhchg16b
    instruction as dependencies, as well, as c11 language capability.

==============================================================================*/


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
    SQ_EVNT_NONE,           // non-event
    SQ_EVNT_INIT,           // first item added to queue
    SQ_EVNT_DEPTH,          // max depth reached
    SQ_EVNT_TIME,           // max time limit reached
    SQ_EVNT_LEVEL           // depth level reached
} sq_event_e;

typedef enum
{
    SQ_IMMUTABLE = 0,       // queue instance unable to modify queue contents
    SQ_READ_ONLY,           // queue instance able to remove items from queue
    SQ_WRITE_ONLY,          // queue instance able to add items to queue
    SQ_READWRITE            // queue instance can add/remove items
} sq_mode_e;

typedef struct sq_item
{
    sh_status_e status;         // returned status
    int64_t length;             // length of data being returned
    void *value;                // pointer to data value being returned
    struct timespec *timestamp; // pointer to timestamp of add to queue
    void *buffer;               // pointer to data buffer
    int64_t buf_size;           // size of buffer
} sq_item_s;

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
    uint32_t max_depth,     // max depth allowed at which add of item is blocked
    sq_mode_e mode          // read/write mode
);


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
);


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
);


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
);


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
);


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
    int signal          // signal to use for event notification
);


/*
    shr_q_add -- add item to queue

    Non-blocking add of an item to shared queue.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_LIMIT    if queue size is at maximum depth
    SH_ERR_ARG      if q is NULL, value is not NULL, or length is <= 0
    SH_ERR_STATE    if q is immutable or read only
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_add(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    void *value,        // pointer to item -- not NULL
    int64_t length      // length of item -- greater than 0
);


/*
    shr_q_add_wait -- attempt to add item to queue

    Attempt add of an item to shared queue, and block if at max depth limit
    until depth limit allows.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL, value is NULL, or length is <= 0
    SH_ERR_NOMEM    if not enough memory to satisfy request
*/
extern sh_status_e shr_q_add_wait(
    shr_q_s *q,         // pointer to queue struct -- not NULL
    void *value,        // pointer to item -- not NULL
    int64_t length      // length of item -- greater than 0
);

/*
    shr_q_add_timedwait -- attempt to add item to queue for specified period

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
    shr_q_s *q,         // pointer to queue struct -- not NULL
    void *value,        // pointer to item -- not NULL
    int64_t length,     // length of item -- greater than 0
    struct timespec *timeout    // timeout value -- not NULL
);


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
);


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
);


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
);


/*
    shr_q_event -- returns active event or SQ_EVNT_NONE when empty

*/
extern sq_event_e shr_q_event(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


/*
    shr_q_explain -- return a null-terminated string explanation of status code

    returns non-NULL null-terminated string error explanation
*/
extern char *shr_q_explain(
    sh_status_e status          // status code
);


/*
    shr_q_exceeds_idle_time -- tests to see if no item has been added within the
    specified time limit

    returns true if limit is equaled or exceeded, otherwise false
*/
extern bool shr_q_exceeds_idle_time(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    time_t lim_secs,            // time limit in seconds
    long lim_nsecs              // time limie in nanoseconds
);


/*
    shr_q_count -- returns count of items on queue, or -1 if it fails

*/
extern int64_t shr_q_count(
    shr_q_s *q                  // pointer to queue struct -- not NULL
);


/*
    shr_q_level -- sets value for queue depth level event generation

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if q is NULL, or level not greater than 0

*/
extern sh_status_e shr_q_level(
    shr_q_s *q,                 // pointer to queue struct -- not NULL
    uint32_t level              // level at which to generate level event
);


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
);



#ifdef __cplusplus
}

#endif


#endif // SHARED_SQ_H_

# libshr

Library of data structures that exist in POSIX shared memory to be used
for interprocess communications.

#### Features of shared_q relative to POSIX IPC queue
- Size of queued item not limited to a preset maximum
- Total size of queue memory limited by system file size limit
- Number of items on queue configurable and maximum governed by semaphore count
maximum
- Number of queues limited only by number of open files per process
- Separate process listeners for arrivals on empty queue versus other monitoring
events
- Event for very initial add, and not just add to empty queue
- Configurable performance related events for monitoring
- Performance related data tracking such as average time on queue, high-water
mark for depth, average queue depth, etc.

#### Completed
- shrq_harness -- multi-threaded test harness for performance/stress testing
shared_q
- sharedq -- command line utility for managing/testing queues

#### Partially working
- shared_q -- interprocess queueing of arbitrary sized data of any type,
performance related counters, as well as, tuning and optimization still to be
completed

#### Planned
- shared_map -- associative map of key/value pairs of arbitrary data
- shared_mem -- memory allocation/deallocation of arbitrary sizes that can be
accessed safely by using a shared token passed between processes

***

## Dependencies
- compiler that supports C11 (gcc or clang)
- x86_64 CPU architecture (little endian integers and cmpxchg16b instruction)

## Build
    ~ $ git clone https://github.com/bkarr/libshr.git
    ~ $ cd libshr
    ~/libshr $ make all

## Test
Unit test:

    ~/libshr $ make check

Performance/stress test:

    ~/libshr $ cd shrq_harness
    ~/libshr/shrq_harness $ ./shrq_harness 2 1 100000
    input SUM[0..100000]=5000050000 output=5000050000
    time:  0.0814
    ~/libshr/shrq_harness $ ./shrq_harness 8 1 625000
    input SUM[0..2500000]=3125001250000 output=3125001250000
    time:  0.9438

 Currently on my Core i7 based laptop with 1 thread per each of the 8 CPUs, the
 harness can write to and read from the queue at a rate of 2.5 million items
 per second.

## Install
    ~/libshr $ sudo make install


## Examples
For right now, unit tests, multi-threaded test harness, and the command line
utility all provide working examples of how to use the library.

More working examples are planned.

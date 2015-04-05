# libshr

Library of data structures that exist in POSIX shared memory to be used
for interprocess communications.

### Partially completed(working)
- shared_q -- interprocess queueing of arbitrary sized data of any type

### Under construction
- sharedq -- command line utility for managing/testing queues

### Planned
- shared_map -- associative map of key/value pairs of arbitrary data
- shared_mem -- memory allocation/deallocation of arbitrary sizes that can be accessed safely  by a shared token
***

## Dependencies
- C11 compiler
- x86_64 CPU architecture (little endian integers and cmpxchg16b instruction)

## Build
    $ git clone https://github.com/bkarr/libshr.git
    $ cd libshr
    $ make all

## Test
Unit test:

    $ make check

Performance test (w/ 2 cpus, 1 thread each, 100000 iterations):

    $ cd shrq_harness
    $ ./shrq_harness 2 1 100000

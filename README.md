# libshr

<!---
<a href="https://scan.coverity.com/projects/4816">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/4816/badge.svg"/>
</a>
-->

Version: 0.2.1

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

#### Working
- shared_q -- interprocess queueing of arbitrary sized data of any type,
still developing unit tests, and tuning and optimization will be ongoing
- shrq_harness -- multi-threaded test harness for performance/stress testing
shared_q
- sharedq -- command line utility for managing/testing queues

#### Partially working

#### Planned
- shared_map -- associative map of key/value pairs of arbitrary data
- shared_mem -- memory allocation/deallocation of arbitrary sizes that can be
accessed safely by using a shared token passed between processes

***

## Dependencies
- compiler that supports C11 standard atomics (gcc 4.9.3)
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
    time:  0.0762
    ~/libshr/shrq_harness $ ./shrq_harness 8 1 630000
    input SUM[0..2520000]=3175201260000 output=3175201260000
    time:  0.9981



 Currently on my Core i7 based laptop with 1 thread per each of the 8 CPUs, the
 harness can write to and read from the queue at a rate of 2.5 million items
 per second.

## Install
    ~/libshr $ sudo make install

## Command line demo

    ~/libshr/sharedq $ ./sharedq help
    sharedq [modifiers] <cmd>

       cmds			 actions
      ------		----------
      add			add item to queue
      create		create queue
      destroy		destroy queue
      drain			drains items in queue
      help			print list of commands
      list			list of queues
      remove		remove item from queue

       modifiers		 effects
      -----------		---------
      -b			blocks waiting for an item to arrive
      -h			prints help for the specified command
      -x			prints output as hex dump
      -v			prints output with headers

    ~/libshr/sharedq $ ./sharedq -h create
    sharedq create <name> [<maxdepth>]

      --creates a named queue in shared memory

      where:
      <name>		name of queue
      <maxdepth>	optional maximum depth, defaults to largest possible value

       modifiers		 effects
      -----------		---------
      -h			    prints help for the specified command
    ~/libshr/sharedq $ ./sharedq create testq
    ~/libshr/sharedq $ ./sharedq -v list

       queues 		 depth 		 size
      --------		-------		------
      testq        	      0		4096

    ~/libshr/sharedq $ ./sharedq add testq Makefile
    ~/libshr/sharedq $ ./sharedq -v list

        queues 		 depth 		 size
       --------		-------		------
       testq       	      1		4096

    ~/libshr/sharedq $ ./sharedq -x remove testq

        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
        -----------------------------------------------
    0000 53 48 45 4C 4C 20 3D 20 2F 62 69 6E 2F 73 68 0A    SHELL = /bin/sh.
    0010 2E 53 55 46 46 49 58 45 53 3A 0A 2E 53 55 46 46    .SUFFIXES:..SUFF
    0020 49 58 45 53 3A 20 2E 63 20 2E 6F 0A 0A 43 43 20    IXES: .c .o..CC
    0030 3D 20 67 63 63 0A 45 58 45 20 3D 20 73 68 61 72    = gcc.EXE = shar
    0040 65 64 71 0A 4C 49 42 20 3D 20 2D 4C 2E 2E 2F 6C    edq.LIB = -L../l
    0050 69 62 20 2D 6C 73 68 72 20 2D 6C 72 74 20 2D 6C    ib -lshr -lrt -l
    0060 70 74 68 72 65 61 64 0A 0A 43 46 4C 41 47 53 20    pthread..CFLAGS
    0070 3D 20 2D 49 2E 2E 2F 69 6E 63 6C 75 64 65 20 2D    = -I../include -
    0080 67 33 20 2D 4F 30 20 2D 73 74 64 3D 67 6E 75 31    g3 -O0 -std=gnu1
    0090 31 20 2D 70 65 64 61 6E 74 69 63 20 2D 57 61 6C    1 -pedantic -Wal
    00A0 6C 20 2D 6F 20 24 28 45 58 45 29 0A 0A 61 6C 6C    l -o $(EXE)..all
    00B0 3A 20 6D 61 69 6E 0A 0A 25 3A 20 25 2E 63 0A 09    : main..%: %.c..
    00C0 40 24 28 43 43 29 20 24 3C 20 24 28 43 46 4C 41    @$(CC) $< $(CFLA
    00D0 47 53 29 20 24 28 4C 49 42 29 0A 0A 63 6C 65 61    GS) $(LIB)..clea
    00E0 6E 3A 0A 09 40 69 66 20 74 65 73 74 20 2D 66 20    n:..@if test -f
    00F0 24 28 45 58 45 29 3B 20 74 68 65 6E 20 5C 0A 09    $(EXE); then \..
        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
        -----------------------------------------------
    0100 09 72 6D 20 2E 2F 24 28 45 58 45 29 3B 20 5C 0A    .rm ./$(EXE); \.
    0110 09 66 69 0A 0A 69 6E 73 74 61 6C 6C 3A 0A 09 40    .fi..install:..@
    0120 63 70 20 24 28 45 58 45 29 20 2F 75 73 72 2F 62    cp $(EXE) /usr/b
    0130 69 6E 2F 2E 0A 0A 2E 50 48 4F 4E 59 3A 20 61 6C    in/....PHONY: al
    0140 6C 20 63 6C 65 61 6E 20 69 6E 73 74 61 6C 6C 0A    l clean install.


## Examples
For right now, unit tests, multi-threaded test harness, and the command line
utility all provide working examples of how to use the library.

There are also examples in C++ and D languages in the examples subdirectory.  The D language examples use the dmd compiler version 2.067.1.  To build the
examples simply enter 'make examples' on the command line in the libshr directory.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

libshr is a C library implementing lock-free data structures in POSIX shared memory for high-performance interprocess communication (IPC). The primary component is `shared_q`, an inter-process queue supporting arbitrary-sized data with lock-free operations using atomic primitives (CAS, DWCAS).

Version: 0.17.3
License: MIT

## Build Commands

```bash
# Build everything (default 64-bit optimized build)
make all

# Run unit tests
make check

# Build and install library and utilities
sudo make install

# Build examples (C++ and D language)
make examples

# Clean build artifacts
make clean
```

### Build Variants

- `make all64` - 64-bit optimized build using x86 CAS/DWCAS instructions
- `make all32` - 32-bit build
- `make rh7` - RedHat 7 compatible build (uses gnu99 instead of gnu11)

The build system creates:
- `lib/libshr.a` - static library
- `lib/libshr.so` - shared library
- `sharedq/sharedq` - command-line queue management utility
- `shrq_harness/shrq_harness` - multi-threaded performance/stress test harness

## Testing

### Unit Tests
```bash
make check
```
Tests are located in `src/test/`:
- `test_internal.c` - Tests internal memory allocation functions
- `test_shared.c` - Tests base shared memory operations
- `test_shrq.c` - Tests shared queue operations

### Performance/Stress Testing
```bash
cd shrq_harness
./shrq_harness <writers> <readers> <iterations_per_writer>

# Example: 4 writers, 4 readers, 530000 iterations each
./shrq_harness 8 1 530000
```
The harness validates correct ordering by summing integer values and reports throughput.

## Architecture

### Core Components

**shared_q** (`src/shared_q.c`, `include/shared_q.h`)
- Lock-free FIFO queue with configurable LIFO mode (adaptive LIFO/CoDel algorithms)
- Supports arbitrary-sized items, vector operations, type metadata
- Event notification system (queue empty, queue full, time limits, depth levels)
- Signal-based notifications for blocking operations (real-time signal support)
- Implemented using lock-free linked list in shared memory with atomic operations

**shared memory management** (`src/shared_int.c`, `src/shared_int.h`)
- Memory-mapped file management (`/dev/shm/` on Linux)
- Lock-free memory allocation with bucket-based free lists
- Dynamic expansion via `mremap()` for growing shared memory regions
- Extent-based memory tracking (linked list of mmapped regions)

**shared utilities** (`src/shared.c`, `include/shared.h`)
- Status codes and type enumerations
- Time manipulation macros (`timespecadd`, `timespecsub`, `timespeccmp`)

### Key Data Structures

**Queue Layout** (in shared memory array):
- Header slots: event queues, item queue head/tail, semaphores, timestamps, notification PIDs/signals
- Lock-free linked list of nodes, each node contains: next pointer, generation counter, event type, data slot reference
- Data slots: variable-size allocation with header (timestamp, unique ID, type, vector count, length)

**Memory Model**:
- Lock-free operations use CAS (Compare-And-Swap) and DWCAS (Double-Word CAS)
- ABA problem prevention via generation counters in DWCAS operations
- Separate head/tail pointers spread across cache lines (version 3 layout)

**Atomic Primitives** (defined in `src/shared_int.h`):
- `CAS(mem, old, new)` - compare-and-swap
- `DWCAS(mem, old, new)` - double-word compare-and-swap (128-bit on x86_64, 64-bit on 32-bit)
- `AFA(mem, v)` - atomic fetch-and-add
- `AFS(mem, v)` - atomic fetch-and-sub
- Two code paths: C11 atomics vs legacy GCC builtins (selected by `__STDC_NO_ATOMICS__`)

### Build System Structure

```
Makefile              # Top-level: iterates through subdirs
├── src/Makefile      # Compiles .c → .o files, runs tests
├── lib/Makefile      # Creates libshr.a and libshr.so from objects
├── sharedq/Makefile  # Builds CLI utility
├── shrq_harness/Makefile  # Builds test harness
└── examples/Makefile # Builds C++ and D examples
```

Common flags: `-std=gnu11 -pedantic -Wall -fPIC -mcx16` (64-bit) or `-m32` (32-bit)

### Command-Line Interface

The `sharedq` utility provides queue operations from the shell:

```bash
./sharedq create <name> [<maxdepth>]  # Create queue
./sharedq add <qname> <data|file>     # Add item
./sharedq remove <qname>              # Remove item
./sharedq -b remove <qname>           # Blocking remove
./sharedq list                        # List queues
./sharedq -v list                     # Verbose listing
./sharedq -x remove <qname>           # Hex dump output
./sharedq destroy <qname>             # Destroy queue
./sharedq monitor <qname>             # Monitor events
./sharedq listen <qname>              # Listen for arrivals
```

## Platform Dependencies

**Current platform support:**
- Requires C11 atomics or GCC atomic builtins
- 64-bit build depends on x86_64 and `cmpxchg16b` instruction (`-mcx16` flag)
- Uses POSIX shared memory (`/dev/shm/`), semaphores, real-time signals
- Originally developed for little-endian (big-endian untested)

**ARM/Graviton porting:**
See `ARM_GRAVITON_PORTING_GUIDE.md` for detailed analysis of memory ordering issues when porting to ARM architecture. Current implementation uses `memory_order_relaxed` which works on x86 TSO but requires stronger ordering (acquire/release semantics) on ARM's relaxed memory model.

## Code Modification Guidelines

- **Atomic operations:** All atomic primitives are centralized in `src/shared_int.h` via macros (CAS, DWCAS, AFA, AFS). Changes to memory ordering semantics should be made there.
- **Memory layout versions:** The queue header uses a VERSION field. Structural changes to shared memory layout require version increment and migration logic.
- **Lock-free invariants:** Modifications to queue operations must maintain lock-free progress guarantees and ABA counter semantics.
- **Shared memory expansion:** The library supports dynamic growth via `resize_extent()` and `expand()`. New allocations must be lock-free compatible.

## Features of shared_q

- No preset maximum item size (unlike POSIX message queues)
- Configurable max queue depth (limited by semaphore count max)
- Separate event listeners for: initial add, add-to-empty, remove-from-full, queue empty, depth levels, time limits
- Blocked calls can use real-time signals for demand/pull processing
- Timestamp tracking and idle time checking
- Optional discard of expired items based on time limit
- CoDel and adaptive LIFO algorithms for traffic spike smoothing
- Vector operations (add/remove multiple items atomically) with type metadata
- Queue name validation to verify it's a valid queue object

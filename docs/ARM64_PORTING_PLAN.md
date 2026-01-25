# ARM64/Graviton Porting Plan

## Overview

This document details every code modification required to produce a single compiled source compatible with both x86_64 and ARM64 (AWS Graviton) architectures. All changes are documented for review before implementation.

**Goal:** Unified codebase using `__atomic_*` builtins with `__ATOMIC_ACQ_REL` ordering, eliminating architecture-specific code paths while maintaining correctness on ARM's relaxed memory model and identical performance on x86_64.

---

## Table of Contents

1. [Summary of Changes](#summary-of-changes)
2. [Phase 1: Core Atomic Primitives](#phase-1-core-atomic-primitives)
3. [Phase 2: Build System Updates](#phase-2-build-system-updates)
4. [Phase 3: Memory Barrier Audit](#phase-3-memory-barrier-audit)
5. [Phase 4: Volatile Cleanup](#phase-4-volatile-cleanup)
6. [Testing Strategy](#testing-strategy)
7. [Appendix: Change Verification](#appendix-change-verification)

---

## Summary of Changes

| File | Type | Changes Required |
|------|------|------------------|
| `src/shared_int.h` | **Critical** | Complete rewrite of atomic primitives section (lines 1-235) |
| `src/Makefile` | **Critical** | Add ARM64 detection and compiler flags |
| `src/test/Makefile` | **Critical** | Add ARM64 test targets |
| `lib/Makefile` | **Required** | Add ARM64 build handling |
| `shrq_harness/Makefile` | **Required** | Add ARM64 build handling |
| `sharedq/Makefile` | **Required** | Add ARM64 build handling |
| `Makefile` (root) | **Required** | Add ARM64 targets |
| `src/shared_int.c` | **Recommended** | Add release fence before data publication (1 location) |
| `src/shared_q.c` | **Recommended** | Add release fences before data publication (2 locations) |

### Files with Volatile Cleanup (Lower Priority)

| File | Locations | Impact |
|------|-----------|--------|
| `src/shared_int.c` | 10 locations | Cosmetic - volatile not harmful, just unnecessary |
| `src/shared_q.c` | 14 locations | Cosmetic - volatile not harmful, just unnecessary |

---

## Phase 1: Core Atomic Primitives

### File: `src/shared_int.h`

This file requires the most significant changes. The current implementation has two code paths (C11 atomics vs legacy builtins) and x86-specific inline assembly for DWCAS.

#### Change 1.1: Remove Conditional Atomic Type Selection

**Current Code (lines 11-19):**
```c
#if (__STDC_VERSION__ >= 201112L)
#include <stdatomic.h>
#endif

#if ((__STDC_VERSION__ < 201112L) || __STDC_NO_ATOMICS__)
typedef volatile long atomictype;
#else
typedef atomic_long atomictype;
#endif
```

**Proposed Change:**
```c
/* Remove stdatomic.h include - using __atomic_* builtins instead */

/* Unified type for both architectures */
typedef volatile long atomictype;
```

**Rationale:** The `__atomic_*` builtins work directly on `volatile long` and don't require `<stdatomic.h>`. This simplifies the code and removes the conditional compilation.

---

#### Change 1.2: Update Architecture-Specific Size Constants

**Current Code (lines 21-27):**
```c
#ifdef __x86_64__
#define SZ_SHIFT 3
#define REM 7
#else
#define SZ_SHIFT 2
#define REM 3
#endif
```

**Proposed Change:**
```c
/* Both x86_64 and aarch64 are 64-bit architectures with 8-byte longs */
#if defined(__x86_64__) || defined(__aarch64__)
#define SZ_SHIFT 3
#define REM 7
#else
/* 32-bit fallback */
#define SZ_SHIFT 2
#define REM 3
#endif
```

**Rationale:** ARM64 (aarch64) is also a 64-bit architecture using 8-byte longs, so it should use the same slot sizing as x86_64.

---

#### Change 1.3: Add Architecture Validation

**Location:** After line 9 (after includes)

**Proposed Addition:**
```c
/*============================================================================
    Architecture Validation
============================================================================*/
#if !defined(__x86_64__) && !defined(__aarch64__) && !defined(__i386__)
    #warning "Untested architecture: only x86_64, i386, and aarch64 are validated"
#endif
```

**Rationale:** Explicitly document supported architectures and warn on untested ones.

---

#### Change 1.4: Update DWORD Type Definition with Alignment

**Current Code (lines 72-77):**
```c
typedef struct {

    atomictype low;
    atomictype high;

} DWORD;
```

**Proposed Change:**
```c
/*
 * DWORD: 128-bit double-word for atomic operations
 *
 * Alignment requirements:
 * - x86_64: CMPXCHG16B requires 16-byte alignment
 * - ARM64: CASP (LSE) requires 16-byte alignment
 *
 * The __int128 member enables unified atomic operations via __atomic_* builtins
 */
typedef union {
    struct {
        atomictype low;
        atomictype high;
    };
    __int128 full;  /* For unified 128-bit atomic access */
} __attribute__((aligned(16))) DWORD;
```

**Rationale:**
- 16-byte alignment required by both `CMPXCHG16B` (x86) and `CASP` (ARM LSE)
- Union with `__int128` enables using `__atomic_compare_exchange_n` on the full 128-bit value
- Maintains API compatibility (`.low`/`.high` members unchanged)

---

#### Change 1.5: Replace All Atomic Primitive Definitions

**Current Code (lines 146-235):**
```c
#if ((__STDC_VERSION__ < 201112L) || __STDC_NO_ATOMICS__)

#define AFS(mem, v) __sync_fetch_and_sub(mem, v)
#define AFA(mem, v) __sync_fetch_and_add(mem, v)

static inline char CAS(
    atomictype *mem,
    atomictype *old,
    atomictype new
)   {
    return __sync_bool_compare_and_swap((long*)mem, *(long*)old, new);
}

#ifdef __x86_64__
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
    : "=a" (old_l), "=d" (old_h)
    : "0" (old_l), "1" (old_h), "b" (new_l), "c" (new_h), "r" (mem), "m" (r)
    : "cc", "memory");
    return r;
}
#else
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

#define AFS(mem, v) atomic_fetch_sub_explicit((atomictype *)mem, v, \
                                              memory_order_relaxed)
#define AFA(mem, v) atomic_fetch_add_explicit((atomictype *)mem, v, \
                                              memory_order_relaxed)
#define CAS(val, old, new) atomic_compare_exchange_weak_explicit(   \
            (atomic_long*)val, (atomic_long*)old, (atomic_long)new, \
            memory_order_relaxed, memory_order_relaxed)
#define DWCAS(val, old, new) atomic_compare_exchange_weak_explicit(val, old, \
              new, memory_order_relaxed, memory_order_relaxed)

#endif
```

**Proposed Change (complete replacement):**
```c
/*============================================================================
    Unified Atomic Operations

    These use __atomic_* builtins which work on both x86_64 and ARM64.

    Memory Ordering:
    - __ATOMIC_ACQ_REL provides acquire-release semantics
    - On x86_64: Generates same instructions as __sync_* (LOCK prefix = full barrier)
    - On ARM64: Generates proper barrier instructions (CASPAL, LDADDAL with LSE)

    This unified approach:
    - Eliminates architecture-specific #ifdef blocks
    - Guarantees correctness on ARM's relaxed memory model
    - Has zero performance cost on x86_64 (identical generated code)
============================================================================*/

/*
 * Atomic Fetch-Add
 * x86_64: Generates LOCK XADD
 * ARM64:  Generates LDADDAL (LSE) or LDAXR/STLXR loop
 */
#define AFA(mem, v) __atomic_fetch_add(mem, v, __ATOMIC_ACQ_REL)

/*
 * Atomic Fetch-Sub
 * x86_64: Generates LOCK XADD (with negated value)
 * ARM64:  Generates LDADDAL (LSE) with negated value
 */
#define AFS(mem, v) __atomic_fetch_sub(mem, v, __ATOMIC_ACQ_REL)

/*
 * Compare-And-Swap (64-bit)
 * x86_64: Generates LOCK CMPXCHG
 * ARM64:  Generates CASAL (LSE) or LDAXR/STLXR loop
 *
 * Returns: 1 on success, 0 on failure
 * On failure, *old is updated with the current value
 */
static inline char CAS(
    atomictype *mem,
    atomictype *old,
    atomictype new
) {
    return __atomic_compare_exchange_n(
        mem,
        old,
        new,
        0,                  /* strong: don't allow spurious failures */
        __ATOMIC_ACQ_REL,   /* success memory order: acquire-release */
        __ATOMIC_ACQUIRE    /* failure memory order: acquire */
    );
}

/*
 * Double-Word Compare-And-Swap (128-bit)
 * x86_64: Generates LOCK CMPXCHG16B (requires -mcx16)
 * ARM64:  Generates CASPAL (LSE) or LDXP/STXP loop
 *
 * Note: The DWORD union must be 16-byte aligned for both architectures.
 *
 * Returns: 1 on success, 0 on failure
 * On failure, *old is updated with the current value
 */
static inline char DWCAS(
    volatile DWORD *mem,
    DWORD *old,
    DWORD new
) {
    return __atomic_compare_exchange_n(
        &mem->full,
        &old->full,
        new.full,
        0,                  /* strong */
        __ATOMIC_ACQ_REL,   /* success memory order */
        __ATOMIC_ACQUIRE    /* failure memory order */
    );
}
```

**Rationale:**
- Unified code path for all architectures (no `#ifdef`)
- `__ATOMIC_ACQ_REL` provides correct ordering on ARM
- On x86_64, generates identical machine code to current implementation
- Using `__atomic_compare_exchange_n` on `__int128` enables compiler to choose optimal instruction

---

### Verification: Generated Assembly

After making these changes, verify the generated assembly:

**x86_64 (expected):**
```asm
# CAS
lock cmpxchg %rsi, (%rdi)

# DWCAS
lock cmpxchg16b (%rdi)

# AFA
lock xadd %rsi, (%rdi)
```

**ARM64 with LSE (expected):**
```asm
# CAS
casal x1, x2, [x0]

# DWCAS
caspal x2, x3, x4, x5, [x0]

# AFA
ldaddal x1, x0, [x2]
```

---

## Phase 2: Build System Updates

### File: `src/Makefile`

#### Change 2.1: Add Architecture Detection and ARM64 Targets

**Current Code (lines 1-35):**
```makefile
SHELL = /bin/bash
.SUFFIXES:
.SECONDARY:

CC = gcc
CFLAGS = -I../include -g3 -fPIC -std=gnu11 -pedantic -Wall
...
all64: CFLAGS += -mcx16 -c -O3 -D__STDC_NO_ATOMICS__
all64: shared shared_int shared_q
```

**Proposed Change:**
```makefile
SHELL = /bin/bash
.SUFFIXES:
.SECONDARY:

CC = gcc
CFLAGS = -I../include -g3 -fPIC -std=gnu11 -pedantic -Wall
OBJECTS = shared.o shared_q.o shared_map.o shared_int.o
TESTDIRS = test
...
LIB = -lrt -lpthread -latomic

# Architecture detection
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),x86_64)
    ARCH_FLAGS = -mcx16
else ifeq ($(UNAME_M),aarch64)
    # Default to generic ARM64 with runtime LSE detection
    # Override with GRAVITON=4 or GRAVITON=5 for optimized builds
    ifeq ($(GRAVITON),5)
        ARCH_FLAGS = -march=armv9.2-a+sve2+lse -mtune=neoverse-v2
    else ifeq ($(GRAVITON),4)
        ARCH_FLAGS = -mcpu=neoverse-v2
    else ifeq ($(GRAVITON),3)
        ARCH_FLAGS = -mcpu=neoverse-v1
    else ifeq ($(GRAVITON),2)
        ARCH_FLAGS = -mcpu=neoverse-n1
    else
        # Generic ARM64: -moutline-atomics provides runtime LSE detection
        ARCH_FLAGS = -march=armv8-a -moutline-atomics
    endif
else ifeq ($(UNAME_M),i686)
    ARCH_FLAGS = -m32
endif

all: all64

rh7: CFLAGS = -I../include -g3 -fPIC -std=gnu99 -Wall
rh7: all64

all64: CFLAGS += $(ARCH_FLAGS) -c -O3
all64: shared shared_int shared_q

all32: CFLAGS += -m32 -c -O1
all32: shared shared_int shared_q shared_map

debug: debug64

debug64: CFLAGS += $(ARCH_FLAGS) -c -O0
debug64: shared shared_int shared_q

debug32: CFLAGS += -m32 -c -O0
debug32: shared shared_int shared_q shared_map

# ... rest unchanged ...
```

**Key Changes:**
1. Removed `-D__STDC_NO_ATOMICS__` (no longer needed with unified `__atomic_*` approach)
2. Added `UNAME_M` architecture detection
3. Added `ARCH_FLAGS` variable set based on architecture
4. Added `GRAVITON` variable for optimized Graviton builds
5. Added `-moutline-atomics` for generic ARM64 builds (runtime LSE detection)

---

#### Change 2.2: Update Test Targets

**Current Code (lines 44-61):**
```makefile
check64: CFLAGS += -mcx16 -DTESTMAIN -O0
check64: clean shared64 checkq64

checkq64: CFLAGS += -mcx16 -c -O0
checkq64: shared64 shared_q
	@cd test && $(MAKE) checkq64 && cd ..
```

**Proposed Change:**
```makefile
check64: CFLAGS += $(ARCH_FLAGS) -DTESTMAIN -O0
check64: clean shared64 checkq64

shared64: CFLAGS += $(ARCH_FLAGS) -c -O0
shared64: shared shared_int

checkq64: CFLAGS += $(ARCH_FLAGS) -c -O0
checkq64: shared64 shared_q
	@cd test && $(MAKE) ARCH_FLAGS="$(ARCH_FLAGS)" checkq64 && cd ..

checkint64: shared64
	@cd test && $(MAKE) ARCH_FLAGS="$(ARCH_FLAGS)" checkint64 && cd ..

checkshr64: shared64
	@cd test && $(MAKE) ARCH_FLAGS="$(ARCH_FLAGS)" checkshr64 && cd ..
```

---

### File: `src/test/Makefile`

#### Change 2.3: Update Test Makefile for Architecture Independence

**Current Code (lines 16-26):**
```makefile
check: checkshr64 checkint64 checkq64

checkq64: CFLAGS += -mcx16 ../shared_int.o ../shared.o ../shared_q.o -o $(TESTQ)
checkq64: clean test_shrq
	./$(TESTQ)
checkint64: CFLAGS += -mcx16 ../shared_int.o -o $(TESTINT)
checkint64: clean test_internal
	./$(TESTINT)
checkshr64: CFLAGS += -mcx16 ../shared.o -o $(TESTSHR)
checkshr64: clean test_shared
	./$(TESTSHR)
```

**Proposed Change:**
```makefile
# ARCH_FLAGS passed from parent Makefile or detected
ARCH_FLAGS ?= $(shell uname -m | grep -q x86_64 && echo "-mcx16" || echo "-march=armv8-a")

check: checkshr64 checkint64 checkq64

checkq64: CFLAGS += $(ARCH_FLAGS) ../shared_int.o ../shared.o ../shared_q.o -o $(TESTQ)
checkq64: clean test_shrq
	./$(TESTQ)
checkint64: CFLAGS += $(ARCH_FLAGS) ../shared_int.o -o $(TESTINT)
checkint64: clean test_internal
	./$(TESTINT)
checkshr64: CFLAGS += $(ARCH_FLAGS) ../shared.o -o $(TESTSHR)
checkshr64: clean test_shared
	./$(TESTSHR)
```

---

### File: `lib/Makefile`

#### Change 2.4: Add ARM64 Handling

**Current Code (lines 13-21):**
```makefile
all64: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
	@gcc -shared -o $(SHARED_LIB) $(OBJECTS)
	@chmod 0755 $(SHARED_LIB)

all32: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
	@gcc -m32 -shared -o $(SHARED_LIB) $(OBJECTS)
	@chmod 0755 $(SHARED_LIB)
```

**Proposed Change:**
```makefile
# Detect if we're on ARM64 to avoid -m32 flag confusion
UNAME_M := $(shell uname -m)

all64: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
	@gcc -shared -o $(SHARED_LIB) $(OBJECTS)
	@chmod 0755 $(SHARED_LIB)

all32: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
ifeq ($(UNAME_M),x86_64)
	@gcc -m32 -shared -o $(SHARED_LIB) $(OBJECTS)
else
	@gcc -shared -o $(SHARED_LIB) $(OBJECTS)
endif
	@chmod 0755 $(SHARED_LIB)
```

---

### File: `shrq_harness/Makefile`

#### Change 2.5: Architecture-Independent Harness Build

**Current Code (lines 14-19):**
```makefile
all64: CFLAGS = -I../include -static -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: main

all32: CFLAGS = -m32 -I../include -static -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all32: main
```

**Proposed Change:**
```makefile
UNAME_M := $(shell uname -m)

all64: CFLAGS = -I../include -static -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: main

all32: CFLAGS = -I../include -static -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
ifeq ($(UNAME_M),x86_64)
all32: CFLAGS += -m32
endif
all32: main
```

---

### File: `sharedq/Makefile`

#### Change 2.6: Architecture-Independent CLI Build

**Current Code (lines 14-17):**
```makefile
all64: CFLAGS = -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: main

all32: CFLAGS = -m32 -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all32: main
```

**Proposed Change:**
```makefile
UNAME_M := $(shell uname -m)

all64: CFLAGS = -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: main

all32: CFLAGS = -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
ifeq ($(UNAME_M),x86_64)
all32: CFLAGS += -m32
endif
all32: main
```

---

### File: `Makefile` (root)

#### Change 2.7: Add ARM64/Graviton Convenience Targets

**Proposed Addition (after line 45):**
```makefile
# ARM64/Graviton optimized builds
graviton2:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) GRAVITON=2 all64; \
		cd ..; \
	done

graviton3:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) GRAVITON=3 all64; \
		cd ..; \
	done

graviton4:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) GRAVITON=4 all64; \
		cd ..; \
	done

graviton5:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) GRAVITON=5 all64; \
		cd ..; \
	done
```

**Update .PHONY (line 83):**
```makefile
.PHONY: all all64 all32 clean check install examples graviton2 graviton3 graviton4 graviton5
```

---

## Phase 3: Memory Barrier Audit

The `__ATOMIC_ACQ_REL` ordering on atomic operations handles most synchronization. However, `memcpy` is not an atomic operation, so we need explicit fences where data is copied before being published atomically.

### File: `src/shared_q.c`

#### Change 3.1: Add Release Fence After Data Copy (copy_value)

**Current Code (lines 249-267):**
```c
static long copy_value(
    shr_q_s *q,
    void *value,
    long length,
    sh_type_e type
)   {
    ...
    if ( current >= HDR_END ) {
        long *array = view.extent->array;
        array[ current + TM_SEC ] = curr_time.tv_sec;
        array[ current + TM_NSEC ] = curr_time.tv_nsec;
        array[ current + UID ] = AFA( &array[ ID_CNTR ], 1);
        array[ current + TYPE ] = type;
        array[ current + VEC_CNT ] = 1;
        array[ current + DATA_LENGTH ] = length;
        memcpy( &array[ current + DATA_HDR ], value, length );
    }

    return current;
}
```

**Proposed Change:**
```c
static long copy_value(
    shr_q_s *q,
    void *value,
    long length,
    sh_type_e type
)   {
    ...
    if ( current >= HDR_END ) {
        long *array = view.extent->array;
        array[ current + TM_SEC ] = curr_time.tv_sec;
        array[ current + TM_NSEC ] = curr_time.tv_nsec;
        array[ current + UID ] = AFA( &array[ ID_CNTR ], 1);
        array[ current + TYPE ] = type;
        array[ current + VEC_CNT ] = 1;
        array[ current + DATA_LENGTH ] = length;
        memcpy( &array[ current + DATA_HDR ], value, length );

        /* Ensure all writes are visible before returning slot for publication */
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }

    return current;
}
```

**Rationale:** The returned `current` slot will be used in `enq_data()` which then performs atomic operations to publish this data. The fence ensures the `memcpy` is visible to other cores before the atomic publish.

---

#### Change 3.2: Add Release Fence After Vector Copy (copy_vector)

**Current Code (lines 302-358):**
```c
static long copy_vector(
    shr_q_s *q,
    sq_vec_s *vector,
    int vcnt
)   {
    ...
    if ( current >= HDR_END ) {
        ...
        for ( int i = 0; i < vcnt; i++ ) {
            ...
            memcpy( &array[ slot ], vector[ i ].base, vector[ i ].len );
            ...
        }
    }

    return current;
}
```

**Proposed Change (after the for loop, before return):**
```c
        for ( int i = 0; i < vcnt; i++ ) {
            ...
            memcpy( &array[ slot ], vector[ i ].base, vector[ i ].len );
            ...
        }

        /* Ensure all writes are visible before returning slot for publication */
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }

    return current;
```

---

### File: `src/shared_int.c`

#### Change 3.3: Add Release Fence in add_end (Optional - ACQ_REL may suffice)

**Current Code (lines 893-934):**
```c
extern void add_end(
    shr_base_s *base,
    long slot,
    long tail
)   {
    atomictype * volatile array = (atomictype*) base->current->array;
    long gen = AFA( &array[ ID_CNTR ], 1 );
    array[ slot ] = slot;
    array[ slot + 1 ] = gen;
    DWORD next_after = { .low = slot, .high = gen };

    while( true ) {
        DWORD tail_before = *( (DWORD * volatile) &array[ tail ] );
        ...
        if ( DWCAS( (DWORD*) &array[ next ], &tail_before, next_after ) ) {
            ...
        }
    }
}
```

**Analysis:** With `__ATOMIC_ACQ_REL` on DWCAS, this should be sufficient. The acquire-release semantics ensure:
- The stores to `array[slot]` and `array[slot+1]` happen-before the DWCAS
- Other threads observing the DWCAS will see those stores

**Recommendation:** No change needed IF using `__ATOMIC_ACQ_REL`. If issues arise during testing, add:
```c
    array[ slot ] = slot;
    array[ slot + 1 ] = gen;
    __atomic_thread_fence(__ATOMIC_RELEASE);  /* Optional: explicit fence */
    DWORD next_after = { .low = slot, .high = gen };
```

---

## Phase 4: Volatile Cleanup (Lower Priority)

The `volatile` keyword is used in several places. With proper atomic operations, these are technically unnecessary but harmless. Clean up is recommended for code clarity but is not required for correctness.

### File: `src/shared_int.c`

| Line | Current | Proposed | Notes |
|------|---------|----------|-------|
| 798 | `volatile long prev = (volatile long) array[ FLAGS ];` | `long prev = array[ FLAGS ];` | Atomic handles visibility |
| 808 | `prev = (volatile long) array[ FLAGS ];` | `prev = array[ FLAGS ];` | Inside CAS retry loop |
| 838 | `volatile long prev = (volatile long) array[ FLAGS ];` | `long prev = array[ FLAGS ];` | Same pattern |
| 848 | `prev = (volatile long) array[ FLAGS ];` | `prev = array[ FLAGS ];` | Inside CAS retry loop |
| 905 | `atomictype * volatile array` | `atomictype *array` | Pointer doesn't need volatile |
| 913 | `*( (DWORD * volatile) &array[ tail ] )` | `*( (DWORD *) &array[ tail ] )` | DWCAS provides ordering |
| 916 | `(atomictype * volatile)` | `(atomictype *)` | Same |
| 929 | `*( (DWORD* volatile) &array[ next ] )` | `*( (DWORD*) &array[ next ] )` | Same |
| 960 | `volatile long * volatile array` | `long *array` | Double volatile unnecessary |
| 1209 | `(volatile long) array[ before.low ]` | `array[ before.low ]` | Inside atomic operation |

### File: `src/shared_q.c`

| Line | Current | Proposed | Notes |
|------|---------|----------|-------|
| 591 | `*(struct timespec * volatile) &array[ EMPTY_SEC ]` | `*(struct timespec *) &array[ EMPTY_SEC ]` | DWCAS provides ordering |
| 602 | Same pattern | Same fix | Inside retry loop |
| 615 | `atomictype * volatile array` | `atomictype *array` | Pointer volatile unnecessary |
| 624 | `*( (DWORD * volatile) &array[ slot ] )` | `*( (DWORD *) &array[ slot ] )` | DWCAS provides ordering |
| 947-948 | `volatile struct timespec` | `struct timespec` | Atomic provides visibility |
| 954 | Same pattern | Same fix | Inside retry loop |
| 1083 | `volatile long * volatile array` | `long *array` | Double volatile unnecessary |
| 3097-3098 | `(volatile time_t)`, `(volatile long)` | Remove casts | Inside DWCAS loop |
| 3602-3603 | Same pattern | Same fix | Inside DWCAS loop |

---

## Testing Strategy

### Step 1: x86_64 Regression Test

```bash
# On x86_64 machine
make clean
make all64
make check

# Stress test
cd shrq_harness && ./shrq_harness 4 4 1000000
```

**Expected:** All tests pass, identical behavior to before changes.

### Step 2: Verify Generated Assembly (x86_64)

```bash
# Compile with assembly output
gcc -S -O3 -mcx16 -I../include src/shared_int.c -o shared_int.s

# Check CAS generates LOCK CMPXCHG
grep -A2 "cmpxchg" shared_int.s

# Check DWCAS generates LOCK CMPXCHG16B
grep "cmpxchg16b" shared_int.s
```

### Step 3: ARM64 Cross-Compile Test

```bash
# Install cross-compiler (on Ubuntu/Debian)
sudo apt install gcc-aarch64-linux-gnu

# Cross-compile
CC=aarch64-linux-gnu-gcc make clean all64

# Verify it compiles without errors
```

### Step 4: ARM64 Native Test (on Graviton)

```bash
# On Graviton instance
make clean
make all64           # Generic ARM64
make check

# Or optimized for Graviton 4
make clean
make GRAVITON=4 all64
make check

# Stress test
cd shrq_harness && ./shrq_harness 4 4 1000000
```

### Step 5: ThreadSanitizer (Optional)

```bash
# Compile with TSan
CFLAGS="-fsanitize=thread -g" make clean all64
make check
```

---

## Appendix: Change Verification

### Checklist Before Implementation

- [ ] Review all changes in this document
- [ ] Confirm `__atomic_*` builtins available (GCC 4.7+)
- [ ] Confirm `__int128` type available on target
- [ ] Test environment has x86_64 machine for regression testing
- [ ] Test environment has ARM64 machine or cross-compiler

### Checklist After Implementation

- [ ] `make clean && make all64` succeeds on x86_64
- [ ] `make check` passes on x86_64
- [ ] `shrq_harness 4 4 1000000` completes without errors on x86_64
- [ ] Cross-compilation for aarch64 succeeds
- [ ] `make check` passes on ARM64 (Graviton)
- [ ] `shrq_harness 4 4 1000000` completes without errors on ARM64
- [ ] No ThreadSanitizer warnings (if tested)

### Files Modified Summary

| File | Lines Changed | Type |
|------|---------------|------|
| `src/shared_int.h` | ~90 lines replaced | Critical |
| `src/Makefile` | ~25 lines added/modified | Critical |
| `src/test/Makefile` | ~10 lines modified | Critical |
| `lib/Makefile` | ~5 lines added | Required |
| `shrq_harness/Makefile` | ~5 lines added | Required |
| `sharedq/Makefile` | ~5 lines added | Required |
| `Makefile` (root) | ~30 lines added | Required |
| `src/shared_q.c` | 2 lines added | Recommended |
| `src/shared_int.c` | 0-1 lines added | Optional |

---

## Document History

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-25 | 1.0 | Initial plan document |

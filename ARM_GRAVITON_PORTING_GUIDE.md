# ARM and AWS Graviton Porting Guide for libshr

This document summarizes the investigation and recommendations for porting libshr's lock-free data structures to ARM architecture, specifically targeting AWS Graviton processors.

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Current x86 Implementation Analysis](#current-x86-implementation-analysis)
3. [ARM Memory Model Differences](#arm-memory-model-differences)
4. [AWS Graviton Processor Specifications](#aws-graviton-processor-specifications)
5. [Compiler Recommendations](#compiler-recommendations)
6. [Code Changes Required](#code-changes-required)
7. [Implementation Strategy](#implementation-strategy)

---

## Executive Summary

The libshr library implements lock-free data structures using atomic primitives (CAS, DWCAS, AFA, AFS). The current implementation works correctly on x86_64 due to its strong memory model (TSO - Total Store Order), but contains several issues that will cause failures on ARM's relaxed memory model.

**Key findings:**
- All atomic operations use `memory_order_relaxed`, which is insufficient on ARM
- The DWCAS implementation uses x86-specific inline assembly (`cmpxchg16b`)
- Publication patterns lack proper release/acquire semantics
- `volatile` is incorrectly used as a synchronization primitive

**Recommendation:** Use unified GCC `__atomic_*` builtins with `__ATOMIC_ACQ_REL` ordering for both x86_64 and ARM64. This provides:
- Single code path for both architectures
- Correct memory ordering on ARM
- Identical performance on x86_64 (generates same instructions as legacy builtins)
- Target Graviton 4/5 with `-mcpu=neoverse-v2` for optimal ARM performance

---

## Current x86 Implementation Analysis

### Atomic Primitives Location

All atomic primitives are defined in `src/shared_int.h`:

| Primitive | C11 Atomics Path | Legacy Builtins Path |
|-----------|------------------|---------------------|
| `AFA` | `atomic_fetch_add_explicit(..., memory_order_relaxed)` | `__sync_fetch_and_add` |
| `AFS` | `atomic_fetch_sub_explicit(..., memory_order_relaxed)` | `__sync_fetch_and_sub` |
| `CAS` | `atomic_compare_exchange_weak_explicit(..., memory_order_relaxed, memory_order_relaxed)` | `__sync_bool_compare_and_swap` |
| `DWCAS` | `atomic_compare_exchange_weak_explicit(..., memory_order_relaxed, memory_order_relaxed)` | Inline asm `cmpxchg16b` |

### Critical Issues Identified

#### Issue 1: Relaxed Memory Ordering (CRITICAL)

**Location:** `src/shared_int.h:225-233`

```c
#define CAS(val, old, new) atomic_compare_exchange_weak_explicit(   \
            (atomic_long*)val, (atomic_long*)old, (atomic_long)new, \
            memory_order_relaxed, memory_order_relaxed)
```

**Problem:** On ARM, `memory_order_relaxed` provides only atomicity, not ordering. Stores made before a CAS may not be visible to other cores that observe the CAS succeeding.

**Impact:** All lock-free algorithms will exhibit data races on ARM.

#### Issue 2: x86-Only DWCAS Implementation (CRITICAL)

**Location:** `src/shared_int.h:176-200`

```c
#ifdef __x86_64__
static inline char DWCAS(volatile DWORD *mem, DWORD *old, DWORD new) {
    __asm__ __volatile__("lock; cmpxchg16b (%6);" ...);
}
#endif
```

**Problem:** ARM64 requires `LDXP`/`STXP` (load/store exclusive pair) or `CASP` (LSE) for 128-bit atomics.

**Impact:** Code will not compile or work correctly on ARM64.

#### Issue 3: Publication Without Barriers (CRITICAL)

**Location:** `src/shared_int.c:905-923` (`add_end` function)

```c
array[ slot ] = slot;           // Store 1
array[ slot + 1 ] = gen;        // Store 2
// ...
DWCAS( (DWORD*) &array[ next ], &tail_before, next_after );  // Publish
```

**Problem:** On ARM, Stores 1 and 2 may be reordered after the DWCAS. Another core could see the DWCAS succeed but read stale/uninitialized data.

#### Issue 4: Data Copy Before Atomic Publish (CRITICAL)

**Location:** `src/shared_q.c:258-265` and similar patterns

```c
memcpy( &array[ current + DATA_HDR ], value, length );
// ... later ...
DWCAS( ... );  // publishes the data
```

**Problem:** The `memcpy` writes have no ordering relationship with the subsequent DWCAS.

#### Issue 5: Misuse of `volatile` (MODERATE)

**Location:** Multiple places throughout codebase

```c
volatile long prev = (volatile long) array[ FLAGS ];
volatile long * volatile array = base->current->array;
```

**Problem:** `volatile` prevents compiler reordering but does not prevent CPU reordering on ARM.

### Summary Table

| Issue | Severity | Files Affected |
|-------|----------|----------------|
| Relaxed memory ordering | Critical | `shared_int.h`, all consumers |
| x86-only DWCAS | Critical | `shared_int.h` |
| No release/acquire on publish | Critical | `shared_int.c`, `shared_q.c`, `shared_map.c` |
| memcpy before atomic | Critical | `shared_q.c`, `shared_map.c` |
| `volatile` misuse | Moderate | Throughout |

---

## ARM Memory Model Differences

### x86 vs ARM Memory Models

| Aspect | x86 (TSO) | ARM (Relaxed) |
|--------|-----------|---------------|
| Store-Store ordering | Preserved | May reorder |
| Load-Load ordering | Preserved | May reorder |
| Store-Load ordering | May reorder | May reorder |
| `LOCK` prefix effect | Full barrier | N/A |
| CAS semantics | Implicit acquire-release | Explicit ordering required |

### Why x86 Code "Accidentally" Works

On x86:
1. The `LOCK` prefix on `cmpxchg` provides a full memory barrier
2. Stores are not reordered with other stores (TSO guarantee)
3. The processor's store buffer drains in order

On ARM:
1. Atomic instructions have explicit ordering suffixes (e.g., `CASAL` for acquire-release)
2. Without barriers, stores can be observed out of order by other cores
3. Load-acquire and store-release must be explicitly requested

---

## AWS Graviton Processor Specifications

### Graviton Family Comparison

| Spec | Graviton 2 | Graviton 3 | Graviton 4 | Graviton 5 |
|------|------------|------------|------------|------------|
| Core | Neoverse N1 | Neoverse V1 | Neoverse V2 | Neoverse V3 |
| Architecture | ARMv8.2-A | ARMv8.4-A | ARMv9.0-A | ARMv9.2-A |
| Cores | 64 | 64 | 96 | 192 |
| LSE Atomics | Yes | Yes | Yes | Yes |
| 128-bit CAS | LDXP/STXP | CASP (LSE) | CASP (LSE) | CASP (LSE) |
| Process | 7nm | 5nm | 5nm | 3nm |
| Memory | DDR4 | DDR5-4800 | DDR5-5600 | DDR5-7200/8400 |

### Graviton 4 Details

- **Core:** ARM Neoverse V2 (Demeter)
- **Architecture:** ARMv9.0-A
- **Cores:** 96 per socket, dual-socket capable (192 vCPUs)
- **L2 Cache:** 2 MB per core
- **Memory Bandwidth:** 537.6 GB/s (12-channel DDR5-5600)
- **PCIe:** 96 lanes PCIe 5.0
- **Instance Types:** R8g, M8g, C8g, X8g

### Graviton 5 Details

- **Core:** ARM Neoverse V3 (Poseidon)
- **Architecture:** ARMv9.2-A
- **Cores:** 192 in single socket (no NUMA)
- **L3 Cache:** ~180 MB shared (5x larger than G4)
- **Memory Bandwidth:** 691.2 GB/s (DDR5-7200), 806.4 GB/s planned (DDR5-8400)
- **Inter-core Latency:** 33% lower than G4
- **Instance Types:** M9g (available), C9g/R9g (planned)
- **Security:** Nitro Isolation Engine with formal verification

### LSE (Large System Extensions) Atomics

All Graviton processors (2+) support LSE atomics, which provide:

| Instruction | Purpose | Equivalent x86 |
|-------------|---------|----------------|
| `CASP` | 128-bit compare-and-swap pair | `CMPXCHG16B` |
| `CASA` | CAS with acquire | `LOCK CMPXCHG` (partial) |
| `CASL` | CAS with release | `LOCK CMPXCHG` (partial) |
| `CASAL` | CAS with acquire-release | `LOCK CMPXCHG` |
| `LDADDA` | Atomic add with acquire | `LOCK XADD` (partial) |
| `SWPAL` | Atomic swap with acquire-release | `LOCK XCHG` |

**Performance:** LSE atomics can be up to 10x faster than load/store-exclusive loops on contended operations.

---

## Compiler Recommendations

### GCC Version

| GCC Version | Recommendation |
|-------------|----------------|
| 14.1+ | **Recommended** - Best Neoverse V2/V3 optimization |
| 12.3+ | Minimum recommended for Graviton 4 |
| 11.x | Acceptable, suboptimal codegen |
| < 11 | Not recommended |

### Compiler Flags

```makefile
# Graviton 4 (Neoverse V2) - Production
CFLAGS_G4 = -mcpu=neoverse-v2 -O3 -fPIC

# Graviton 5 (Neoverse V3) - When GCC adds full support
# Until then, neoverse-v2 works well
CFLAGS_G5 = -march=armv9.2-a+sve2+lse -mtune=neoverse-v2 -O3 -fPIC

# Graviton 3 (Neoverse V1)
CFLAGS_G3 = -mcpu=neoverse-v1 -O3 -fPIC

# Graviton 2 (Neoverse N1)
CFLAGS_G2 = -mcpu=neoverse-n1 -O3 -fPIC

# Generic ARM64 with runtime LSE detection (portable)
CFLAGS_ARM64_GENERIC = -march=armv8-a -moutline-atomics -O3 -fPIC

# x86_64 (existing)
CFLAGS_X86 = -mcx16 -O3 -fPIC
```

### Flag Explanations

| Flag | Purpose |
|------|---------|
| `-mcpu=neoverse-v2` | Optimize for Graviton 4's Neoverse V2 cores |
| `-march=armv9.2-a+sve2+lse` | Target ARMv9.2 with SVE2 and LSE extensions |
| `-moutline-atomics` | Runtime detection of LSE (portable across ARM64) |
| `-mcx16` | Enable `cmpxchg16b` on x86_64 |
| `-O3` | Full optimization |
| `-fPIC` | Position-independent code (required for shared library) |

### Recommended Makefile Structure

```makefile
# Detect architecture
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),x86_64)
    # -mcx16 enables CMPXCHG16B for 128-bit atomics
    ARCH_FLAGS = -mcx16
else ifeq ($(UNAME_M),aarch64)
    # Default to generic ARM64, override with GRAVITON=4 or GRAVITON=5
    ifeq ($(GRAVITON),5)
        ARCH_FLAGS = -march=armv9.2-a+sve2+lse -mtune=neoverse-v2
    else ifeq ($(GRAVITON),4)
        ARCH_FLAGS = -mcpu=neoverse-v2
    else ifeq ($(GRAVITON),3)
        ARCH_FLAGS = -mcpu=neoverse-v1
    else
        # -moutline-atomics provides runtime LSE detection for portability
        ARCH_FLAGS = -march=armv8-a -moutline-atomics
    endif
endif

CFLAGS = $(ARCH_FLAGS) -O3 -fPIC -std=gnu11 -pedantic -Wall
```

**Note:** The `-D__STDC_NO_ATOMICS__` flag is no longer needed since we use `__atomic_*` builtins which work uniformly on both architectures.

---

## Code Changes Required

### 1. Updated Atomic Primitives (`src/shared_int.h`)

Replace the existing atomic definitions with **unified, architecture-independent** implementations using `__atomic_*` builtins:

```c
#ifndef SHAREDINT_H_
#define SHAREDINT_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <shared.h>

/*============================================================================
    Architecture Validation
============================================================================*/

#if !defined(__x86_64__) && !defined(__aarch64__)
    #error "Unsupported architecture: requires x86_64 or aarch64"
#endif

/*============================================================================
    Type Definitions
============================================================================*/

/* Both x86_64 and ARM64 are 64-bit architectures */
#define SZ_SHIFT 3
#define REM 7
#define LONG_BIT (CHAR_BIT * sizeof(long))

typedef volatile long atomictype;

/*
 * DWORD: 128-bit double-word for atomic operations
 *
 * - Must be 16-byte aligned for:
 *   - x86_64: CMPXCHG16B instruction requirement
 *   - ARM64: CASP (LSE) instruction requirement
 * - The __int128 member enables unified atomic operations via __atomic_* builtins
 * - The low/high struct members preserve API compatibility with existing code
 */
typedef union {
    struct {
        atomictype low;
        atomictype high;
    };
    __int128 full;
} __attribute__((aligned(16))) DWORD;

/*============================================================================
    Unified Atomic Operations

    These use __atomic_* builtins which work on both x86_64 and ARM64.

    Memory Ordering:
    - __ATOMIC_ACQ_REL provides acquire-release semantics
    - On x86_64: Generates same instructions as __sync_* (LOCK prefix = full barrier)
    - On ARM64: Generates proper barrier instructions (e.g., CASPAL, LDADDAL)

    This unified approach:
    - Eliminates architecture-specific #ifdef blocks
    - Guarantees correctness on ARM's relaxed memory model
    - Has zero performance cost on x86_64
============================================================================*/

/*
 * Atomic Fetch-Add / Fetch-Sub
 *
 * x86_64: Generates LOCK XADD
 * ARM64:  Generates LDADDAL (LSE) or LDAXR/STLXR loop
 */
#define AFS(mem, v) __atomic_fetch_sub(mem, v, __ATOMIC_ACQ_REL)
#define AFA(mem, v) __atomic_fetch_add(mem, v, __ATOMIC_ACQ_REL)

/*
 * Compare-And-Swap (64-bit)
 *
 * x86_64: Generates LOCK CMPXCHG
 * ARM64:  Generates CASAL (LSE) or LDAXR/STLXR loop
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
        __ATOMIC_ACQ_REL,   /* success: acquire-release */
        __ATOMIC_ACQUIRE    /* failure: acquire */
    );
}

/*
 * Double-Word Compare-And-Swap (128-bit)
 *
 * x86_64: Generates LOCK CMPXCHG16B (requires -mcx16)
 * ARM64:  Generates CASPAL (LSE) or LDXP/STXP loop
 *
 * Note: The DWORD union must be 16-byte aligned for both architectures.
 */
static inline char DWCAS(
    volatile DWORD *mem,
    DWORD *old,
    DWORD new
) {
    return __atomic_compare_exchange_n(
        (volatile __int128 *)&mem->full,
        &old->full,
        new.full,
        0,                  /* strong */
        __ATOMIC_ACQ_REL,   /* success: acquire-release */
        __ATOMIC_ACQUIRE    /* failure: acquire */
    );
}

/* ... rest of shared_int.h unchanged ... */

#endif /* SHAREDINT_H_ */
```

### Why Unified __atomic_* Builtins?

| Aspect | Legacy `__sync_*` | Unified `__atomic_*` |
|--------|-------------------|----------------------|
| GCC support | 4.1+ | 4.7+ (widely available) |
| Memory ordering | Always full barrier | Explicit (we choose ACQ_REL) |
| x86_64 codegen | LOCK CMPXCHG, etc. | **Identical** instructions |
| ARM64 codegen | Full barriers (overkill) | Optimal LSE instructions |
| Code complexity | `#ifdef` per arch | Single implementation |
| Correctness | Correct but inflexible | Correct and portable |

### Generated Assembly Comparison

**x86_64:**
```asm
# CAS with __atomic_compare_exchange_n(..., __ATOMIC_ACQ_REL, ...)
lock cmpxchg %rsi, (%rdi)    # Same as __sync_bool_compare_and_swap

# DWCAS with __atomic_compare_exchange_n on __int128
lock cmpxchg16b (%rdi)       # Same as inline asm version

# AFA with __atomic_fetch_add(..., __ATOMIC_ACQ_REL)
lock xadd %rsi, (%rdi)       # Same as __sync_fetch_and_add
```

**ARM64 (Graviton 4/5 with LSE):**
```asm
# CAS
casal x1, x2, [x0]           # Compare-and-swap with acquire-release

# DWCAS
caspal x2, x3, x4, x5, [x0]  # Compare-and-swap pair with acquire-release

# AFA
ldaddal x1, x0, [x2]         # Atomic add with acquire-release
```

### 2. Memory Barrier for Data Publication

Add explicit barriers before publishing data that was written via `memcpy`:

```c
/* In functions that copy data before atomic publish */
memcpy(&array[current + DATA_HDR], value, length);

/* Ensure memcpy is visible before we publish the pointer */
__atomic_thread_fence(__ATOMIC_RELEASE);

/* Now safe to publish */
DWCAS((DWORD*)&array[slot], &before, after);
```

### 3. DWORD Alignment

Ensure all DWORD instances are properly aligned:

```c
/* Stack allocation */
_Alignas(16) DWORD before = { 0 };
_Alignas(16) DWORD after = { .low = value, .high = gen };

/* Or use the union definition which has built-in alignment */
```

---

## Implementation Strategy

### Phase 1: Core Atomic Changes (Required)

1. Update `src/shared_int.h` with unified `__atomic_*` builtins (as shown above)
2. Update DWORD typedef with `__attribute__((aligned(16)))` and `__int128 full` member
3. Update Makefile with architecture detection and appropriate flags
4. Compile on x86_64 to verify no regressions
5. Cross-compile or compile on ARM64 to verify it builds

**Changes are minimal:** The unified approach requires fewer code changes than architecture-specific implementations.

### Phase 2: Barrier Audit (Recommended)

The `__ATOMIC_ACQ_REL` ordering on CAS/DWCAS operations handles most synchronization needs. However, review these patterns:

1. **Data publication via memcpy:** Add `__atomic_thread_fence(__ATOMIC_RELEASE)` after `memcpy` and before the atomic that publishes the pointer
2. **Reading published data:** The acquire semantics on failed CAS already provides this, but verify the pattern
3. **Non-atomic reads after atomic:** If reading non-atomic data after observing an atomic change, ensure proper ordering

**Note:** With `__ATOMIC_ACQ_REL` on all atomic operations, explicit fences are only needed around `memcpy` and similar non-atomic bulk operations.

### Phase 3: Testing and Validation (Required)

1. **x86_64 regression testing:**
   ```bash
   make clean && make all
   make check  # Run existing unit tests
   ```

2. **ARM64 functional testing (on Graviton or cross-compile):**
   ```bash
   make clean && make GRAVITON=4 all
   make check
   ```

3. **Stress testing:**
   ```bash
   # Queue stress test
   cd shrq_harness && ./shrq_harness 4 4 1000000

   # Map stress test (if on testhash branch)
   cd shrmap_harness && ./shrmap_harness 4 4 100000 64 256
   ```

4. **ThreadSanitizer (optional but recommended):**
   ```bash
   CFLAGS="-fsanitize=thread" make clean all
   make check
   ```

### Phase 4: Performance Optimization (Optional)

Once functional correctness is verified, consider these optimizations:

1. **Relax ordering for pure counters:**
   ```c
   /* ID_CNTR is only used for unique IDs, ordering doesn't matter */
   #define AFA_RELAXED(mem, v) __atomic_fetch_add(mem, v, __ATOMIC_RELAXED)
   ```

2. **Profile on target hardware:**
   - Compare throughput: x86_64 vs Graviton 4 vs Graviton 5
   - Identify if any atomic operations are hot spots
   - LSE atomics on Graviton should be very fast

3. **Cache line alignment:**
   - Consider aligning hot fields to 64-byte boundaries
   - Avoid false sharing between frequently-updated fields

---

## References

### AWS Graviton Documentation
- [AWS Graviton Getting Started](https://github.com/aws/aws-graviton-getting-started)
- [AWS EC2 Graviton](https://aws.amazon.com/ec2/graviton/)
- [Graviton5 Announcement](https://www.aboutamazon.com/news/aws/aws-graviton-5-cpu-amazon-ec2)

### ARM Architecture
- [ARM Neoverse V2](https://www.arm.com/products/silicon-ip-cpu/neoverse/neoverse-v2)
- [ARM Neoverse V2 TRM](https://www.scs.stanford.edu/~zyedidia/docs/arm/neoverse_v2_trm.pdf)
- [ARM Neoverse Wikipedia](https://en.wikipedia.org/wiki/ARM_Neoverse)

### Compiler Documentation
- [GCC ARM Options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)
- [GCC Atomic Builtins](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)
- [NVIDIA Grace Compiler Guide](https://docs.nvidia.com/grace-perf-tuning-guide/compilers.html)

### Memory Model Resources
- [C11 Memory Model](https://en.cppreference.com/w/c/atomic/memory_order)
- [ARM Memory Model](https://developer.arm.com/documentation/102336/latest)

---

## Document History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2026-01-01 | 1.1 | Claude Code | Simplified to unified `__atomic_*` approach for both architectures |
| 2025-12-31 | 1.0 | Claude Code | Initial investigation and recommendations |

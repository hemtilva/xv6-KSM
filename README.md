# xv6 Mini-KSM: Kernel Samepage Merging & Page Deduplication

A kernel-level memory deduplication system for **xv6-riscv** (inspired by Linux KSM). It identifies user-space memory pages containing identical contents across processes, merges them into a single read-only physical frame, and transparently duplicates them on write via Copy-on-Write (COW).

---

## 🎯 Project Goals

- **Memory Conservation:** Share identical anonymous memory pages across processes to reduce physical memory consumption.
- **Transparent COW:** Maintain process isolation using page faults (`scause 15`) to un-merge pages upon modification.
- **Multicore Safety:** Prevent race conditions during remapping without native hardware TLB shootdown support in xv6.
- **Empirical Evaluation:** Measure memory savings, scan overhead, and COW break latency under varying workloads.

---

## 🏗️ Architecture & Core Components

```
User Space:   [ksmd (Daemon)]    [ksmtest (Tests)]    [ksmbench (Benchmarks)]
                     |                   |                   |
Syscall API:   ksmscan()           ksmenable()         ksminfo()
                     +-------------------+-------------------+
                                         |
Kernel Space:  [kernel/ksm.c] -> Page Scanner, Fixed Hash Table, Safe Merge Protocol
               [kernel/vm.c]  -> COW Mapping, copyout handling, refcount-aware unmap
               [kernel/trap.c]-> Store Page Fault Handler (COW Break)
               [kernel/kalloc.c]-> Physical Page Refcount Table
```

### 1. Refcount & Copy-on-Write (COW) Foundation
- **Refcount Array:** Per-physical-page reference counters in `kernel/kalloc.c` indexed by `(pa - KERNBASE) / PGSIZE`.
- **COW Flag:** Uses a reserved software bit (`PTE_COW`) in page table entries (`kernel/riscv.h`).
- **Fault Handler:** Catches store page faults in `usertrap()`. If `refcount > 1`, allocates a private replica; if `refcount == 1`, restores write permissions (`PTE_W`) in-place.

### 2. Page Scanner & Safe Merge Protocol (`kernel/ksm.c`)
- **Page Walk:** Iterates through user page tables (`0` to `p->sz`) of opted-in processes.
- **Two-Stage Match:** Hashes candidate pages into a fixed-size table, followed by a byte-by-byte comparison to eliminate hash collision false positives.
- **Race Prevention:**
  1. Revoke write permissions (`clear PTE_W, set PTE_COW`).
  2. Re-hash and re-verify page contents to guard against concurrent writes.
  3. Remap target PTE to the existing physical page and increment its reference counter.
  4. Free the duplicate physical frame and issue local TLB flushes (`sfence.vma`).

### 3. System Calls
| Syscall | Purpose |
|---|---|
| `int ksmenable(int on)` | Opts the calling process in/out of KSM scanning |
| `int ksmscan(void)` | Triggers an immediate one-shot scan pass |
| `int ksminfo(struct ksmstat *st)` | Queries statistics (pages scanned, merged, shared, savings, cow breaks) |

---

## 📁 Key File Changes

| Component | Target Files | Responsibility |
|---|---|---|
| **PTE Definitions** | `kernel/riscv.h` | Define `PTE_COW` mask |
| **Physical Allocator** | `kernel/kalloc.c` | Track page refcounts on `kalloc()` / `kfree()` |
| **Virtual Memory** | `kernel/vm.c` | COW-aware `uvmcopy()`, `uvmunmap()`, and `copyout()` |
| **Trap Handling** | `kernel/trap.c` | COW page fault resolution in `usertrap()` |
| **Process Tracking** | `kernel/proc.c`, `kernel/proc.h` | KSM opt-in flag per process and safe process traversal |
| **KSM Engine** | `kernel/ksm.c` (new) | Page scanner, hash table, and safe merge protocol |
| **User Utilities** | `user/ksmtest.c`, `user/ksmd.c`, `user/ksmbench.c` | Verification tests, background scanner daemon, benchmark suite |

---

## 🚀 Scope Tiers

- **Minimum Tier (Core):** COW fork, refcounting, one-shot `ksmscan()`, static hash table, merge/un-merge, `ksminfo()`, and core test suite.
- **Target Tier (Full):** Background scanner daemon (`ksmd`), per-process opt-in (`ksmenable`), zero-page fast path, multicore safe-merge protocol, and benchmark suite with graphs.
- **Stretch Goals:** Adaptive scanning rates, two-level stable/unstable tracking trees, and hash algorithm comparisons (Sum vs. FNV-1a vs. CRC).

---

## 🧪 Testing & Verification

- **Correctness:** Identical page merging, copy-on-write isolation on write, hash collision handling, and process exit / `sbrk` shrink cleanup.
- **Regression:** Compatibility with stock `usertests`.
- **Concurrency:** Multi-CPU stress testing with active concurrent writes during scanning passes.

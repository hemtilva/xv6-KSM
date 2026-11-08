# xv6 Page Deduplication (Mini-KSM)

A kernel samepage merging (KSM) mechanism for xv6-riscv. Mini-KSM scans anonymous user memory pages across processes, identifies pages with identical contents, merges them into a single read-only physical frame, and transparently un-merges them on write via Copy-on-Write (COW).

## Overview

In multi-process workloads running identical binaries or shared data structures, physical memory is often consumed by duplicate pages. Mini-KSM implements inter-process page deduplication modeled after Linux KSM, tailored for xv6's RISC-V paging subsystem.

Key mechanisms:
- Per-physical-page reference counting.
- Copy-on-Write fault handling using a reserved PTE software bit (`PTE_COW`).
- An address-space scanner operating over opted-in processes.
- Two-stage candidate validation: page hashing followed by byte-for-byte verification.
- A multicore-safe merge protocol addressing xv6's lack of inter-processor TLB shootdown.

## Architecture and Design

### 1. Reference Counting and Copy-on-Write
- **Reference Table:** An array in `kernel/kalloc.c` tracks references to physical frames between `KERNBASE` and `PHYSTOP`. `kalloc()` initializes refcounts to 1; `kfree()` decrements the count and only frees the underlying frame when the count drops to 0.
- **PTE Flag:** Uses a reserved software bit in the RISC-V page table entry (`PTE_COW`).
- **Fault Handling:** In `kernel/trap.c` (`usertrap`), a store page fault (`scause == 15`) on a `PTE_COW` page allocates a private frame if refcount > 1, or re-enables `PTE_W` in-place if refcount == 1.
- **Kernel Copyout:** `copyout()` in `kernel/vm.c` resolves COW mappings before writing into user buffers (e.g., during `read` or `pipe` syscalls).

### 2. Scanner and Safe Merge Protocol (`kernel/ksm.c`)
- **Address Space Walk:** Iterates through valid user pages (`0` to `p->sz`) of processes that have opted in via `ksmenable`.
- **Verification:** Computes a page hash, probes a fixed-size kernel hash table, and performs a full 4096-byte comparison on candidate matches.
- **Merge Sequence:**
  1. Revoke write permissions (`clear PTE_W`, `set PTE_COW`) on the target PTE.
  2. Re-verify page contents to ensure no concurrent write occurred before write revocation took effect.
  3. Remap the target PTE to the existing identical physical frame, increment that frame's refcount, and free the duplicate frame.
  4. Flush the local TLB via `sfence.vma`.
  5. Multicore synchronization: merges are restricted to processes not currently running on another core, or held under the target process's lock.

### 3. System Calls
- `int ksmenable(int on)`: Enables or disables KSM scanning for the calling process.
- `int ksmscan(void)`: Triggers an immediate scan pass over all opted-in address spaces.
- `int ksminfo(struct ksmstat *st)`: Returns runtime metrics, including pages scanned, pages merged, shared pages, and COW break events.

## Source Tree Modifications

| File | Changes |
|---|---|
| `kernel/riscv.h` | Add `PTE_COW` definition (software bit). |
| `kernel/kalloc.c` | Implement physical page refcount array; update `kalloc()` and `kfree()`. |
| `kernel/vm.c` | Update `uvmcopy()`, `uvmunmap()`, and `copyout()` for COW semantics. |
| `kernel/trap.c` | Handle COW store faults (`scause 15`) in `usertrap()`. |
| `kernel/proc.h`, `kernel/proc.c` | Add KSM opt-in flag to `struct proc`; process iteration helpers. |
| `kernel/ksm.c` | Page scanner, hash table, verification, merge protocol, and stats tracking. |
| `kernel/syscall.h`, `kernel/syscall.c`, `kernel/sysproc.c` | Implement `ksmenable`, `ksmscan`, and `ksminfo` syscall dispatch. |
| `user/ksmtest.c` | Test suite covering merging, write isolation, exit cleanup, and stress tests. |
| `user/ksmd.c` | Background user-space daemon invoking `ksmscan()` periodically. |
| `user/ksmbench.c` | Benchmarking utility measuring memory savings and scan overhead. |

## Scope Tiers

- **Minimum:** COW fork with refcounting, one-shot `ksmscan` syscall, static hash table, merge and un-merge on write, `ksminfo`, and core correctness tests.
- **Target:** Minimum tier plus user-space scanner daemon (`ksmd`), per-process opt-in (`ksmenable`), zero-page special casing, multicore-safe merge synchronization, and benchmark suite.
- **Stretch:** Adaptive scan frequencies, two-level stable/unstable page tracking, and comparative hash function evaluation.

## Verification

The test harness (`user/ksmtest.c`) verifies:
- Merging of duplicate pages across distinct processes.
- Copy-on-write isolation: writes to merged pages do not corrupt peer processes.
- False positive rejection: distinct pages with colliding hashes are rejected by byte verification.
- Lifecycle correctness: refcount reclamation during `fork`, `exit`, and `sbrk` deallocation.
- Multicore stability: scanning and merging while worker processes execute concurrent writes.
- Regression testing: clean pass across stock xv6 `usertests`.

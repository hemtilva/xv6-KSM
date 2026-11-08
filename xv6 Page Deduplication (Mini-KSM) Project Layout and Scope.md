# xv6 Page Deduplication (Mini-KSM): Project Layout and Scope

*Assumes xv6-riscv (the current MIT version) running under QEMU. Adjust file names if you use the x86 version.*

## 1. Project summary

**Goal:** Add a kernel feature to xv6 that finds user memory pages with identical contents across processes, merges them into a single read-only physical page, and transparently splits them again (copy-on-write) when any process writes.

**Why it is non-trivial:** It combines virtual memory, page-fault handling, reference counting, hashing, and concurrency reasoning, and it needs an evaluation of design trade-offs, not just a working implementation.

**Relation to existing work:** The mechanism mirrors Linux KSM. The original contribution is the scanner design, the safety argument for merging on a multicore kernel without TLB shootdown, and the measured evaluation.

## 2. Scope

### In scope

- Copy-on-write (COW) support in the kernel, as the foundation
- Per-physical-page reference counting
- A page scanner that walks user page tables of opted-in processes
- Page hashing plus byte-by-byte verification before any merge
- Merge and un-merge (break on write) logic
- A `ksminfo` system call for statistics
- An opt-in system call so only chosen processes are scanned
- Test programs and a benchmark program
- A written evaluation with graphs

### Out of scope

- Swapping pages to disk
- Merging file-backed or kernel pages (xv6 has no page cache, so only anonymous user pages matter)
- Huge pages or NUMA
- Linux-style stable and unstable trees (listed as a stretch goal only)
- Any change to the file system or scheduler

## 3. Scope tiers

Each tier is a complete, demonstrable project. Later tiers are optional.

| Tier | Contents | Approx. size |
| --- | --- | --- |
| **Minimum (guaranteed)** | COW fork with refcounts, one-shot scan syscall, hash table, merge, un-merge on write, `ksminfo`, core tests | \~400 LOC |
| **Target (propose this)** | Minimum, plus a background scanner process, per-process opt-in, zero-page special case, safe-merge protocol, benchmark and graphs | \~700-900 LOC |
| **Stretch** | Adaptive scan rate, two-level (stable/unstable) tracking, hash function comparison | +200-300 LOC |

## 4. System design

### 4.1 High-level architecture

- **User space:** `ksmd` (daemon), `ksmtest` (correctness tests), `ksmbench` (benchmarks)
- **System calls:** `ksmenable(int on)`, `ksmscan(void)`, `ksminfo(struct ksmstat *)`
- **Kernel scanner (`kernel/ksm.c`):** walks pages, hashes, matches, merges
- **Kernel VM layer (`vm.c`, `trap.c`, `kalloc.c`):** COW bit, refcounts, fault handling

### 4.2 Core data structures

- **Refcount array:** one counter per physical page, indexed by `(pa - KERNBASE) / PGSIZE`, protected by a lock
- **COW marker:** use a reserved software bit (RSW) in the PTE, defined as `PTE_COW` in `riscv.h`
- **Hash table:** a fixed-size static table of `{hash, pa}` entries, since xv6 has no general-purpose heap beyond whole pages
- **Stats struct:** `{scanned, merged, shared, saved_pages, cow_breaks, scan_ticks}`

### 4.3 Key algorithms

**Scan pass**

1. For each opted-in process, lock it and walk its page table from address 0 to `p->sz`
2. Skip invalid, non-user, trampoline, and trapframe mappings
3. Hash each candidate page
4. Look up the hash; if there is a match, verify with a full byte comparison
5. If identical, run the merge protocol; otherwise insert into the hash table

**Merge protocol (the safety-critical part)**

1. Clear `PTE_W` and set `PTE_COW` on the candidate page
2. Re-hash and re-compare after clearing write permission, so a concurrent write cannot slip in
3. Point the second PTE at the first page's physical address, read-only and COW
4. Increment the refcount, then free the duplicate page
5. Flush the TLB on the local hart and handle other harts as described in section 6

**COW fault handler (in `usertrap`)**

1. On a store page fault (scause 15), check that the PTE is valid and has `PTE_COW`
2. If the refcount is 1, restore `PTE_W` and clear `PTE_COW` (no copy needed)
3. Otherwise allocate a new page, copy the data, map it writable, and decrement the old page's refcount
4. Kill the process only if the fault is not a legitimate COW fault

## 5. Files to be modified or added

| File | Change |
| --- | --- |
| `kernel/riscv.h` | Define `PTE_COW` |
| `kernel/kalloc.c` | Refcount array; `kalloc` sets count to 1; `kfree` decrements and frees only at 0 |
| `kernel/vm.c` | `uvmcopy` shares pages instead of copying; `copyout` handles COW pages; `uvmunmap` respects refcounts |
| `kernel/trap.c` | Handle COW store faults in `usertrap` |
| `kernel/proc.c`, `proc.h` | Per-process opt-in flag; helper to iterate processes safely |
| **`kernel/ksm.c` (new)** | Scanner, hash table, merge logic, stats |
| `kernel/syscall.c`, `syscall.h`, `sysproc.c` | Register and implement the three new syscalls |
| `kernel/defs.h` | Prototypes |
| `user/user.h`, `user/usys.pl` | User-side stubs |
| **`user/ksmtest.c`, `ksmd.c`, `ksmbench.c` (new)** | Tests, daemon, benchmark |
| `Makefile` | Add new user programs to `UPROGS` |

## 6. Concurrency and correctness plan

This section is where most of the project's thinking lives, so the write-up should discuss it explicitly.

- **Race between compare and remap:** handled by revoking write permission first, then re-verifying.
- **Stale TLB entries on other harts:** xv6 has no TLB shootdown. Options to evaluate and document:
  1. Only merge pages belonging to processes that are not currently RUNNING (check state under `p->lock`)
  2. Hold the target process's lock for the whole merge
  3. Run with `CPUS=1` while debugging, then justify the multicore approach
- **Refcount discipline:** audit every path that releases user memory: `exit`, `uvmunmap`, `sbrk` shrink, failed `fork`, failed `exec`. Add a debug assertion that the sum of refcounts matches the number of mapped user pages.
- **Kernel writes to user memory:** `copyout` must break sharing before writing, which also covers `read` and `pipe` syscalls.
- **Lock ordering:** document it (process lock before refcount lock before kmem lock) and never violate it.

## 7. Milestones and timeline

The timeline assumes a beginner to kernel work, roughly 8 weeks at 6-8 hours per week. Compress or stretch as your deadline requires.

| Week | Milestone | Done when |
| --- | --- | --- |
| 1 | **Learn and set up.** Build xv6, boot in QEMU, read the book chapters on page tables, traps, and locking. Add a trivial syscall as a warm-up. | Your own `hello` syscall works |
| 2 | **Refcounts.** Add the refcount array and update `kalloc`/`kfree`. | System boots and `usertests` still passes |
| 3 | **COW fork.** `uvmcopy`, `PTE_COW`, fault handler, `copyout`. | `cowtest` and `usertests` pass |
| 4 | **Send the proposal update to your instructor** (if not already confirmed). Build the scanner skeleton, hashing, and the hash table. | One-shot scan reports identical pages |
| 5 | **Merge and un-merge.** Implement the safe-merge protocol and `ksminfo`. | Two processes with identical data merge; a write un-merges correctly |
| 6 | **Tests and hardening.** Edge cases, stress test, refcount audit. | All tests pass repeatedly with multiple CPUs |
| 7 | **Target features.** Background daemon, opt-in, zero-page case. Benchmarks and graphs. | Benchmark results collected |
| 8 | **Write-up and demo.** Report, README, demo script, cleanup. | Everything delivered |

**Checkpoint rule:** if you are behind at the end of week 5, drop the Target features and finish the Minimum tier well.

## 8. Testing plan

| Test | What it proves |
| --- | --- |
| Identical pages in two processes | Merge happens; stats show savings |
| Write after merge | Writer gets a private copy; the other process's data is unchanged |
| Different pages with the same hash prefix | Full compare prevents false merges |
| Fork then merge | COW and dedup coexist |
| `exit` of one sharer | Refcount decrements; remaining process still works |
| `sbrk` shrink on merged region | No leak or double free |
| `read()` into a merged buffer | `copyout` breaks sharing correctly |
| Many processes, concurrent writes during scanning | No crashes, no corruption on multicore |
| Free-page count before and after | Memory is genuinely returned |
| Existing `usertests` | No regressions |

## 9. Evaluation plan

The evaluation is what turns a re-implementation into a project with its own findings.

**Experiments**

1. **Memory saved vs. number of identical processes** (for example 2, 4, 8, 16 copies of a program that fills a buffer with a fixed pattern)
2. **Memory saved vs. data similarity** (100%, 75%, 50%, 25% of pages identical)
3. **Scanner cost:** ticks spent scanning versus pages merged
4. **Hash function comparison:** a simple sum, FNV-1a, and CRC-style hash, comparing collision rate and scan time
5. **COW break cost:** time for the first write after a merge versus a normal write

**Output:** 3 to 5 graphs and a short discussion of what the numbers show, including limitations.

## 10. Deliverables

1. Source code in a Git repository, with a clear branch or patch against stock xv6
2. Test programs and a script that runs them
3. Benchmark programs and raw results
4. A written report (5 to 8 pages) covering design, concurrency reasoning, evaluation, and limitations
5. A short live demo (about 5 minutes)

## 11. Success criteria

- **Pass:** COW fork works, one-shot scan merges identical pages, un-merge on write is correct, and `usertests` passes
- **Good:** the above, plus a background scanner, per-process opt-in, and a multicore-safe merge protocol with a written argument for why it is safe
- **Excellent:** the above, plus a benchmark-driven evaluation with graphs and an honest discussion of trade-offs

## 12. Risks and mitigations

| Risk | Likelihood | Mitigation |
| --- | --- | --- |
| Refcount bugs causing leaks or panics | High | Add an audit function early; test after every change |
| Concurrency bugs only appearing on multiple CPUs | Medium | Develop with `CPUS=1`, then test with `CPUS=3` or more |
| Learning curve eats the schedule | High | Do the MIT 6.S081 COW lab first; use the checkpoint rule |
| Scope creep | Medium | Freeze the Target tier; treat Stretch as optional |
| Hard-to-debug hangs | Medium | Use `printf` tracing and `make qemu-gdb` |

## 13. Resources

- The xv6 RISC-V book, especially the chapters on page tables, traps and page faults, and locking
- MIT 6.S081 labs (the COW lab is your direct foundation)
- Linux KSM documentation (for background and terminology)
- The xv6-riscv source itself, which is small enough to read

## 14. Open questions to confirm with your instructor

1. Is the xv6 RISC-V version acceptable (versus x86 or Linux)?
2. Is building on top of a standard COW implementation acceptable if it is part of the deliverable?
3. What is the deadline, and what format do you expect for the report and demo?
4. Is a solo project expected, and is a 5-minute demo sufficient?

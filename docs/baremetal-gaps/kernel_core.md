<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ZXV kernel core and kernel/user boundary: gap audit

Audited: origin/main at 4bea0160 (read-only). Paths are relative to the repo root.
The audit read the code and ran the host test `test_el0_userspace` (it passes, but it does not
cover slot reuse, pool exhaustion or the fault-halt path below). This report repeats nothing
already in TURNKEY_OS_GAPS.md or OS_FEATURE_AUDIT_2026-10-12.md, except where it corrects them.

## 0. The short version

- **arm64** has a real but small EL0 user space. It has per-process TTBR0 tables, SVC, W^X for
  user pages, timer preemption and an ELF loader with Ed25519-signed `.zsp` (`run hello.zsp`).
  It is a single-CPU, fixed-window prototype:
  - every process lives in the first 2 MB of VA;
  - user memory comes from a 2 MB bump pool that is never freed;
  - process slots are never reclaimed;
  - FP/SIMD state is not saved;
  - a user fault can still halt the kernel.
- **x86_64** has a ring-3 *self-test* and nothing else. It runs one hand-assembled 34-byte
  program, once, in the kernel's own page tables. There is:
  - no timer interrupt (IF is never set);
  - no scheduler, process table or per-process CR3;
  - no NX, SMEP or SMAP;
  - an ELF loader that only accepts AArch64.
- **Neither arch** has the following:
  - a physical memory manager fed by the UEFI memory map (the stubs fetch the map and throw it away);
  - a kernel heap that can free (`malloc` is a 1 MB static bump arena per translation unit);
  - SMP, spinlocks, IOAPIC/LAPIC, MSI or GIC SPIs for devices (all drivers are polled);
  - mmap or shared memory;
  - spawn/wait syscalls.
- **`lpres_hex.c` (the lpx table and quarantine log) is not linked into either bare-metal image**
  (Makefile.arm64:271,513 link only `lpres_core.c` and `lpres.c`; Makefile.x86_64:268 links only
  `lpres_core.c`). Every design below needs it linked first. It is about 214 lines plus SHA-256,
  which is already in the image.

### Corrections to existing docs

| Doc | Says | Actually |
|---|---|---|
| docs/ARCHITECTURE.md:212 | "Capability-checked syscalls, scheduler, memory ✅" | Partial on arm64 only. x86_64 has no scheduler. Neither arch has memory management beyond static pools (sections 1–3 below). |
| TURNKEY_OS_GAPS.md row 16 | "processes are built into the kernel (user_procs.c)" | Half true. arm64 *also* loads separate ELF / signed `.zsp` binaries from ZXVFS (`kernel_main_arm64.c:3790-3830` → `proc_create_from_elf`, `el0_userspace.c:658`). That works only when a **virtio-mmio** disk carries ZXVFS. The golden disc is attached as **virtio-blk-pci** (`build_system/uefi_boot_check.sh:54`), and arm64 has no PCI at all (`virtio_blk.c:1`, no ECAM code). So on the golden disc arm64 sees no disk and cannot load an app. x86_64 cannot load a user binary at all (`elf.c:104` accepts only EM_AARCH64). |
| TURNKEY_OS_GAPS.md row 8 | "EL0 per-process page tables + W^X, x86 ring3.c" | arm64: true for **user** pages only. Kernel RAM is EL1-executable (no PXN on data, no SCTLR.WXN; `arm64_mmu.c:145-172`). x86: no W^X anywhere. All memory is RWX at ring 0, there is no EFER.NXE, and the ring-3 code page is writable + executable (`ring3.c:126-131`, `boot.asm:84-94`). |
| BAREMETAL.md "event cycle running" | event cycle | On x86 the final loop is `hlt` with IF=0 (`kernel_main_x86_64.c:328-335`; `sti` is never called), so the `heartbeat` branch is dead code. The CPU sleeps until NMI. |

## 1. Ranked gap table

Status: **exists**, **partial**, **stub** or **missing**. "Size" is a rough count of new or changed
C lines (asm included), excluding tests. Rows marked **★** are on the critical path for
*boot golden disc → login → run an app as a separate user process*.

| Rank | Gap | Status | Evidence (file:line) | Size | Depends on |
|---|---|---|---|---|---|
| 1 ★ | Boot handoff carries the UEFI memory map, RSDP and the kernel's full extent | missing | x86 `efi_stub.c:150-161`: map fetched, then dropped. `efi_stub.c:128-133`: reserves only `ksize` (the file; .bss is not reserved) and ignores AllocatePages failure. `zxv_bootinfo.h` has framebuffer fields only. arm64 stub is the same (`arm64/efi_stub.c:77-117`). | 150 | none |
| 2 ★ | Physical frame allocator (PMM) | missing (arm64: bump pool; x86: none) | `el0_userspace.c:65-77`: 2 MB static pool, bump only, never freed. `mm/mm.c` + `mm.h:10-14` is i386 legacy (1024-entry 32-bit page directories, HEAP_START 0x200000), called only by `arch/arm` and `boot/`. | 350 | 1 |
| 3 ★ | Per-process address space object + generic page-table walker | partial (arm64 fixed 2 MB window) / missing (x86) | arm64 `el0_userspace.c:188-190` refuses any VA outside L2 slot 0. ELF window 0x1000–0x200000 (`elf.h:59-60`) **overlaps the stack** 0x80000–0x90000 (`el0_userspace.h:65-67`), so a segment there silently replaces stack pages. Two PT_LOADs sharing a page each allocate a fresh page, losing the first one's bytes (`el0_userspace.c:619-627`). x86 has one CR3 for everything; `make_user_page` just flips U/S on identity-mapped kernel .bss (`ring3.c:115-132`). | arm64 400, x86 450, common VMA 300 | 2 |
| 4 ★ | x86_64 process runtime: timer IRQ, IRQ vectors, context switch, scheduler, ELF x86 | missing | `ring3_asm.S:46`: IF forced to 0 in ring 3. `ring3.c:206-209`: YIELD/SLEEP are no-ops. `ring3.c:312-316`: PIC masked, no LAPIC. `idt/idt.c` and `timer/timer.c` are i386 PIT code, linked but never called. `sched/sched.c:117`: "context switch would happen here". `elf.c:104`: EM_AARCH64 only. | 900 | 2, 3 |
| 5 ★ | Process lifecycle: slot reclaim, spawn, wait/exit status, kill | partial | TERMINATED is never returned to UNUSED (`el0_sched.c:64`; `proc_create` scans for UNUSED only, `el0_userspace.c:403,665`). After 32 launches nothing more can run, and the 2 MB pool runs out after about 15 (`proc_create` uses 32 pages each). Failed creates leak PT slot and pages (`el0_userspace.c:436,696`). There is no spawn/wait syscall (`zxv_syscall_abi.h:82-95`); launching happens only through the kernel shell `run`. | 450 | 2, 3 |
| 6 ★ | Fault isolation that never halts the kernel | partial | A user fault kills only that process **only if another process is READY at that instant**. Otherwise `arm64_exceptions.c:297-312` prints "kernel idle" and falls through into the fatal `while(1) halt()`. The P-TERM shell spends most of its life SLEEPING (`user_procs.c:164`), so a faulting `run` app will usually halt the whole machine. Any EL1 fault halts (`arm64_exceptions.c:183-187`). x86 halts on any fault outside the self-test (`ring3_asm.S:177-184`). | 80 | 5 |
| 7 ★ | Real user/kernel split: CAP_EXEC runs the **kernel** shell at EL1 | partial | `SYS_EXEC` → `kernel_shell_exec` at EL1 with IRQs masked (`el0_userspace.c:1021-1047`). Every `proc_create` process gets `CAP_TRUSTED_ALL` (`:505`). The EL0 shell is therefore a thin front for an all-powerful kernel shell. A login process built this way would not be separated. | 200 (plus moving commands to user space) | 8, 10 |
| 8 ★ | Syscall ABI v1.1: handles, pointer+len I/O, consistent errors, verdict | partial | 12 calls (`zxv_syscall_abi.h:82-95`). WRITE takes one byte. READ is gated by CAP_FS but reads the UART (`el0_userspace.c:1005-1019`). Error codes disagree with the table: EFAULT returned as `-2`, which is ZXV_EPERM (`:1040,1100,1116`), and ENOSYS as `-38` (`:1150`) against ZXV_ENOSYS `-1`. The table and permit logic (`syscall.c:34-43`) are solid. | 600 | 3, 5 |
| 9 ★ | FP/SIMD context save | missing | `CPACR_EL1.FPEN=0b11` enables FP at EL0 (`arm64/boot.s:66-72`), but `cpu_context_t` holds only x0–x30/sp/pc/pstate (`el0_userspace.h:134-139`). One process's NEON registers leak into the next, and any compiled C app (memcpy uses SIMD) corrupts others. x86 has no FXSAVE/XSAVE. | 200 | 4 |
| 10 ★ | Load apps from the golden disc | partial | x86: `hw_storage_selftest` can read a file from the ESP FAT32 (`kernel_main_x86_64.c:306`) but nothing loads an executable from it. arm64: no PCI, so no disk (see corrections). | 150 loader glue; arm64 PCI/virtio-pci about 500 (drivers thread) | 4, 5 |
| 11 ★ | Input and display to a user process (login UI) | missing | Input is UART polling inside the SVC (`el0_userspace.c:1013-1017`). x86 USB keyboard is polled in the kernel only. No call grants framebuffer access. | 250 (event channel + fb grant) | 12, 14 |
| 12 | IPC: handle-based channels with backpressure | partial | Per-PID 8×248 B queue inside the PCB (`el0_userspace.h:36-38,125-131`). A full queue returns -1 and the message is dropped (`el0_userspace.c:805`). A RECV that blocks with no other READY process keeps running at EL0 while marked BLOCKED and later resumes from a stale context (`:841-843,1124-1139`), the same bug already fixed for SLEEP at `:1076-1081`. | 400 | 8 |
| 13 | Kernel heap with free + OOM behaviour | missing | `freestanding.h:80-89`: `static` 1 MB bump arena **inside a static inline function**, so each translation unit that calls malloc gets its own private 1 MB of BSS. There is no `free` (`freestanding.c:25-26`). OOM returns NULL with no policy. | 350 | 2 |
| 14 | Demand paging, anonymous mmap, shared memory | missing | No VMA concept and no lazy fault path. Every EL0 data abort is fatal to the process (`arm64_exceptions.c:237-277`). | 450 | 3, 8 |
| 15 | Kernel hardening: kernel W^X, guard pages, SMEP/SMAP/PAN, CR0.WP | missing / partial | arm64: .text read-only only in block 0 (`arm64_mmu.c:166-172`). Data is EL1-executable. No PAN: `copy_from_user` dereferences raw (`el0_userspace.c:578-581`); safe today only because of the pre-validation walk. One 1 MB kernel stack with no guard page; overflows into .rodata have happened (`el0_userspace.c:266-273,345-352`). x86: no NXE, SMEP or SMAP. | 200 | 3 |
| 16 | Device interrupts: LAPIC/IOAPIC/MSI-X (x86), GIC SPIs/ITS (arm64) | missing (timer only on arm64) | `hwdrv.h:41`: "every driver polling loop". The PCI `irq_line` is read but unused (`hwdrv_core.c:179`). arm64 registers only IRQ_TIMER (`kernel_main_arm64.c:840`). A whole kernel event cycle runs inside the timer ISR (`arm64_exceptions.c:89-90,325-326`). | x86 700, arm64 300 | 4 |
| 17 | SMP: AP/PSCI bring-up, per-CPU data, locks, IPIs, TLB shootdown | missing | No `psci`, `cpu_on`, `lapic`, spinlock or atomics anywhere in `kernel/arch` or `kernel/src` (grep). TLB: full `tlbi vmalle1is` on every switch with ASID unused (`el0_userspace.c:220-225`). | 1000 | 4, 16 |
| 18 | Threads | missing | One context per process (`el0_userspace.h:142-170`). | 350 | 5, 17 |
| 19 | Signals / async notification | missing | None. A fault only prints (`arm64_exceptions.c:250-276`). | 150 | 12 |
| 20 | Hardware description for real arm64 (ACPI MADT/GTDT or DTB) | missing | `board_profile.h:109-151`: fixed QEMU-virt addresses. | 400 | 1 |

## 2. Build first, and why

What stops a user from booting the golden disc, logging in and running an app as a separate
process today:

1. **x86_64 (the first physical target)** cannot run a second ring-3 program at all. It has no
   timer, scheduler, per-process CR3, frame allocator or x86 ELF.
2. **arm64** cannot see the golden disc. Even with a disk, it runs out of process slots and user
   pages after about 15 launches, and a faulting app usually halts the kernel.
3. **On both**, "login" would have to be either kernel code or a CAP_EXEC process that drives
   the EL1 shell. Neither is a separated user space.

**Wave 1** is the foundation: ranks 1, 2, 3, 5, 6, plus linking `lpres_hex.c`. It is about
1,800 lines. These rows are arch-neutral except for the page-table walkers. Everything else
assumes they exist, and they fix the three arm64 defects that bite immediately: pool exhaustion,
slot exhaustion and fault-halt.

**Wave 2** is x86_64 parity and the ABI: ranks 4, 8, 9 and 10. It is about 1,850 lines. Port
`el0_sched.c` (already portable C) to x86 with a LAPIC timer, add the x86 ELF machine type and
FXSAVE, and add the v1.1 ABI with handles and verdicts.

**Wave 3** gets to login: ranks 11, 12 and 7. It is about 850 lines. It adds an input event
channel and a framebuffer grant, a login process using `seats/` (on a branch), and shrinks
CAP_EXEC to a narrow "spawn by path" call.

**Later** is ranks 13–20. Heap, mmap, IRQs and SMP matter for a *good* OS. A single-CPU,
polled, static-heap system can still log in and run an app.

## 3. Paraconsistent designs for the top gaps

The lpx base codes form an integer chain: F=00 < N=01 < B=10 < T=11. `lpres_conjoin` is
**min** on that chain (`lpres.c:65-84`), and `lpx_action` is a 64-byte table lookup. Every
design below reduces to:
1. compute two evidence bits per item;
2. take the min across items;
3. index the table once.

That costs a few ALU ops and one L1-resident load per decision.

### 3.1 Boot handoff v2 (rank 1): convention, plus one paraconsistent check

`zxv_bootinfo_t` v2 adds:
- `mmap_phys`, `mmap_size`, `desc_size`, `desc_ver`, copied by the stub into a page it allocated
  *before* ExitBootServices;
- `rsdp`;
- `kernel_lo` and `kernel_hi`, where `kernel_hi` is `__bss_end`, not the file size.

The stub must fail the boot (print and stay in firmware) if AllocatePages for the kernel window
fails, rather than copy over firmware memory.

**Paraconsistent part.** The UEFI map and the kernel's own knowledge are two witnesses. A range
that the UEFI map calls Conventional but the kernel extent or bootinfo also covers is not "free".
It is **B**, and the PMM fences it (3.2). Overlapping UEFI descriptors with different types are
**B** for the same reason. Real firmware does emit these, and convention silently picks one.

### 3.2 PMM with four-valued frame state (rank 2)

**Data.** Two bits per 4 KB frame, using the lpx base code:

| Code | State | Meaning |
|---|---|---|
| 01 | N | free, unclaimed |
| 11 | T | owned (one owner) |
| 10 | B | contradictory: double-free, double-claim, map overlap; fenced for life |
| 00 | F | not usable: firmware, MMIO, bad |

Cost is 64 KB per GB of RAM. On top of that:
- a 1-bit-per-64-frames summary of "any N in this word", so allocation is `ctz` on the summary,
  then `ctz` on `w & ~(w>>1) & 0x5555…` (the N pattern);
- a per-CPU magazine of 32 frames, so the bitmap is touched once per 32 allocations.

**Contiguous DMA (8 MB pool today, `hwplat_x86.c:8`).** Keep a small buddy *only* for
order ≥ 4 requests carved at boot. A buddy is the right convention for contiguity. The
four-state bitmap stays the ground truth.

**Verdicts change behaviour:**
- `pmm_free(f)` on a frame that is already N gives N∧(claim) → B. The frame is **fenced**
  (never handed out again), and an lpx tag `B/forbidden` goes to the quarantine log with the
  caller's return address as subject. Convention would corrupt the free list or panic. ZXV keeps
  running, fences one page, and leaves an audit trail.
- `pmm_alloc()` with no N frames returns **N/WAIT**, not NULL. The caller's process is parked
  on the "frames" wait token (3.5) and woken by the next free. Only a caller in the kernel's
  reserved class gets **F** (fail now). This is the OOM policy: wait first, refuse explicitly,
  never kill silently.

### 3.3 Address space + page-fault verdict (ranks 3, 14, 15)

**Data.** `zx_as_t` holds:
- root table PA, ASID/PCID;
- `vma[ZX_VMA_MAX=32]`: a sorted array of `{lo, hi, prot:3, kind:2 (anon | image | stack | shm), lazy:1, shm_handle}`, searched by binary search (5 compares);
- `frames_owned`.

Each arch provides `pt_map/pt_unmap/pt_protect/pt_walk` (about 200 lines each). The user range
is the full lower half: arm64 T0SZ=16 (48-bit), x86 canonical low half with a per-process PML4
whose upper 256 entries are shared with the kernel. Every stack VMA has an unmapped guard page
below it, and the ELF window is validated against existing VMAs, which fixes the stack overlap.

**Fault classification.** The two evidence bits are *vma_says_ok* (the access is within a VMA
and its prot) and *pte_consistent* (the PTE state agrees with the VMA and the PMM owns the frame).

| Case | Base | lpx action | Behaviour |
|---|---|---|---|
| In a lazy VMA, PTE empty, frame available | T | PROCEED | zero-fill / copy-on-write, retry the instruction |
| In a lazy VMA, no frame (PMM gave N) | N | WAIT | park on frames token; retried on wake (no OOM kill) |
| Outside every VMA, or prot violation (write RX, exec NX, user→kernel) | F | REFUSE | terminate *this* process, exit status = fault verdict to parent (3.5) |
| VMA says ok but PTE/PMM disagree (RO PTE on RW non-COW VMA; frame in PMM state N while mapped) | B | ESCALATE | **hold** the process (not killed, not run), qlog record, notify supervisor channel |
| EL1/ring-0 fault on a user VA outside the copy-user window, or on a guard page of a kernel stack | F + forbidden | ROLLBACK | abort the syscall, return `EFAULT` + forbidden tag, quarantine; the kernel continues |

The **B** row is what convention lacks. A kernel bug or a bit flip that makes the page tables
contradict the VMA list is neither a user error (F) nor fixable (T). Convention either kills the
process (blaming it wrongly) or panics. Holding it keeps evidence and keeps the machine up.

**copy_from_user and copy_to_user** use the convention: an exception-fixup table, with PAN set
on arm64 (`SCTLR.SPAN=0`, `PSTATE.PAN=1`, toggled only inside copy routines, or LDTR/STTR) and
SMAP STAC/CLAC on x86. The pre-walk in `proc_user_range_check` can then go, and the fault path
above supplies the verdict. Honest note: fixup tables plus PAN/SMAP are simply best practice
here.

### 3.4 Capabilities as grant/deny evidence (ranks 7, 8)

**Data.** Each process holds two masks, `G` (grants: manifest, parent, seat) and `D` (denies:
seat policy, parental, user revoke). The per-bit base is `(g<<1) | !d`, so:
- G only → T
- D only → F
- both → **B**
- neither → **N**

For a call needing `need`:
```
if (need & D & ~G)        v = F;   /* explicit deny             */
else if (need & ~(G | D)) v = N;   /* undecided                 */
else if (need & D)        v = B;   /* granted AND denied        */
else                      v = T;
```
This is exactly the min over required bits, so it matches `lpres_conjoin`: four ANDs and three
branches, which replaces `zxv_syscall_permit`'s single test (`syscall.c:41`) and keeps the table.

**Behaviour.**
- T runs the call.
- F returns `EPERM`.
- **N** runs the first-use consent path: the caller is parked (WAIT) and a request goes to the
  session's consent service. This is the camera/mic prompt in OS_FEATURE_AUDIT, derived instead
  of special-cased.
- **B** is ESCALATE. A manifest grants a right a seat policy forbids, so the call is held and
  shown to the owner as a conflict. It is never silently resolved in either direction.

Capability bits also grow from 5 to 32 (net, camera, mic, fb, input, spawn, shm, …). `CAP_EXEC`
is replaced by `CAP_SPAWN`, which starts a signed image by path. Nothing gets the EL1 shell.

### 3.5 Process lifecycle as the lpx phase cycle (ranks 5, 6, 19)

**Data.** `proc.tag` (lpx_t) replaces `proc_state_t`. The phase is the lifecycle, and X3
(Gray steps) forbids jumps:

| Phase | Lifecycle meaning |
|---|---|
| potential (00) | slot free |
| emerging (01) | address space building, ELF mapping, verifying signature |
| manifest (11) | admitted; base and polarity decide running vs waiting |
| dissolving (10) | exited or faulted; resources being freed; exit verdict kept for the parent |

The step dissolving → potential happens only when the parent's `wait` reaps it, or after
`reap_timeout` ticks if the parent is gone (reparented to init). This **structurally** fixes the
never-reclaimed slots (`el0_sched.c:64`). A jump from manifest to potential is refused by
`lpx_advance`, so a slot cannot be reused while still referenced.

**Exit status** is the tag itself plus an `int32`:
- T: clean exit
- F: refused (fault, killed)
- B: held conflict, then abandoned
- N: never ran

`wait(handle)` returns both, so a supervisor learns *why*, not just a number.

**Fault path fix (rank 6).** After terminating, if no process is READY, idle in `wfi` with IRQs
on until one is (the code already does this for SYS_EXIT at `el0_userspace.c:942-965`). Never
fall into the fatal halt.

**Signals equivalent.** No async signal handlers, which suits an integer-only kernel with a
single kernel stack. Instead, every process has a supervisor channel, and the kernel posts
`{pid, tag, reason}` there on fault, kill, hold or OOM-wait. "Kill" is a message plus a forced
dissolving step. This is deliberately not POSIX signals; those are a known source of
re-entrancy bugs, and channels are cheaper here.

### 3.6 Scheduler on tags (ranks 4, 17)

**Queues:** run[8 priorities] with an 8-bit non-empty mask, plus `wait`, `held` and `gone`.
The `pick` step is `ctz(mask)`, O(1). The tick rule is `lpx_action(proc.tag)`:

| Action | Tag | Scheduler effect |
|---|---|---|
| PROCEED | T manifest | stays in run queue |
| WAIT | N, T-entropic, B-negentropic | `wait` queue, keyed by a 32-bit wait token (frames, channel id, timer deadline, consent id); no CPU. **T-entropic** (a weakening truth, e.g. a process whose budget evidence is falling) is demoted one priority level, not parked. |
| ESCALATE | B neutral/entropic | `held`: never scheduled, listed in the UI as "waiting for you", memory kept |
| REFUSE / ROLLBACK | F, forbidden | dissolving |

Wake-up is "post evidence to a token". The waker updates the base (often N→T) and moves the
process back to `run`, with no polling. This removes the shell's `SYS_SLEEP(10)` busy-poll
(`user_procs.c:160-165`).

**Kernel stack.** Keep the current **single kernel stack per CPU**, a continuation-style kernel
(`el0_userspace.c:274-276` already resets SP_EL1 on every ERET). Under this model a syscall
cannot block inside the kernel; it returns an **N/WAIT** verdict, and the scheduler parks the
caller. This fits the verdict model and saves 8–16 KB per process. Honest cost: long kernel
operations (FS, `kernel_shell_exec`) must be split into restartable steps or moved to kernel
tasks. That is real work, about 300 lines across zxvfs/shell.

**Preemption.** The timer ISR does the scheduling decision only. The kernel event cycle that
runs inside the ISR today (`arm64_exceptions.c:89,325`) moves to a kernel task in run queue
priority 0.

**SMP is convention:**
- per-CPU area through GS base / TPIDR_EL1;
- ticket spinlocks;
- per-CPU run queues with work stealing;
- IPIs for TLB shootdown (with ASIDs/PCIDs, so the flush is per address space).

The only paraconsistent use is in AP bring-up. Each AP's self-report (APIC ID/MPIDR) is checked
against the MADT/DT entry:
- match → T, scheduled;
- timeout → N: one retry, then F;
- mismatch → B: never scheduled, logged.

### 3.7 Syscall result verdict (rank 8)

**ABI v1.1** (MINOR bump, numbers kept):
- `x0`/`rax` stays the value or negative `ZXV_E*`, so C wrappers stay normal;
- the **lpx tag** comes back in `x1`/`rdx` (6 bits; upper bits zero, reserved).

The libzxv wrapper acts on `lpx_action(tag)`:

| Action | Wrapper behaviour |
|---|---|
| PROCEED | return the value |
| REFUSE | return the error |
| WAIT | the kernel has already parked the caller; on wake the call is re-issued transparently. This replaces EAGAIN busy loops. |
| ESCALATE | return `ZXV_EHELD` with a handle to the conflict, which the app can show |
| ROLLBACK | the caller's state was not changed (the kernel guarantees no partial effect); quarantined |

**New calls** (all handle-based):
- `HANDLE_CLOSE`, `CHAN_CREATE/SEND/RECV`
- `VM_MAP(anon|shm, len, prot)`, `VM_UNMAP`, `VM_PROTECT` (refuses W|X)
- `SPAWN(path_handle, caps⊆G)`, `WAIT(proc_handle)`
- `READ/WRITE(handle, ptr, len)`
- `INPUT_SUBSCRIBE`, `FB_GRANT`

The error-code fix comes first, about 20 lines: `-2` becomes `ZXV_EFAULT` and `-38` becomes
`ZXV_ENOSYS` at `el0_userspace.c:1040,1100,1116,1150`. Today a bad pointer reads as "permission
denied".

Honest note: a verdict is not needed on every hot call to be useful. `x1` costs one register
write. The gain is that N (not yet), F (no) and B (contested) stop being merged into
`EAGAIN`/`EIO`.

### 3.8 IPC (rank 12): mostly convention

**Channels** are Zircon/seL4-style: a pair of endpoint handles and a bounded ring of 16 × 256 B
messages, which can carry handles (rights ⊆ sender's).

**Backpressure:** SEND on a full ring returns **N/WAIT** and parks the sender on the channel
token, instead of today's silent drop.

**Each message carries the sender's lpx tag** (1 byte). A receiver can `lpx_and` the tags of
inputs it combines, so a B input propagates as B to its output. This is the "work tag" rule X1.

**Shared memory** is a `shm` object handle mapped through `VM_MAP`. Two writable mappings
without the `SHM_MULTIWRITE` right are **B**, so the second map is ESCALATE rather than silently
allowed.

Honest note: the ring, the handles and rights-subset transfer are the best-known design. The
tag is metadata on top.

### 3.9 Kernel heap (rank 13): convention

**Slab caches** (32 B–2 KB, power of two) over PMM pages, plus page-granular large allocations.
`free` is real. The per-TU static arenas in `freestanding.h:80-89` become a single global
allocator, which also stops wasting up to 1 MB of BSS per caller translation unit.

**Paraconsistent part:** each slab keeps a 2-bit state per object using the same encoding as the
PMM. A double free is **B**: the object is fenced and logged, not reused. Allocation failure is
N (wait) for process-context callers and F for ISR callers.

### 3.10 Where convention simply wins (say so, do not decorate)

These are standard engineering with no useful four-valued twist:
- LAPIC/IOAPIC/MSI-X and GIC/ITS programming;
- ticket locks;
- PCID/ASID TLB tagging;
- XSAVE/NEON lazy-save via CR0.TS / CPACR trap, which also fixes the cross-process FP leak at
  low cost;
- SMEP/SMAP/PAN, NX/PXN/WXN, guard pages;
- exception fixup tables for copy-user.

Building them conventionally and tagging only the *decisions* (fault outcome, capability,
allocation, lifecycle, scheduling class) is the efficient split. Tagging data or every interrupt
would add cost without changing any outcome (`lpres_hex.h` X1 says the same).

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tri-Isolate: run a plugin in its own process

Tri-Isolate runs each Z-Wasm plugin in a separate worker process. The
plugin still runs under Z-Wasm, with the same validator, fuel meter and
host functions. What changes is that a crash, a hang or a bug that escapes
the Wasm sandbox happens in the worker process, not in the host. The host
keeps everything that matters:

- **It validates the module itself** before any worker sees it. A float
  plugin is refused and no process is started.
- **It pays for the fuel.** Each call gets
  `min(call fuel, floor(the budget's remaining))` from the host's own rmag
  budget, and the host charges what the worker reports using. If the
  worker misbehaves, the host charges the whole allotment.
- **It kills the worker** with SIGKILL when a call runs out of fuel, traps,
  goes past its wall-clock timeout, or breaks the protocol, and then reaps
  it. A runaway plugin cannot keep a host thread waiting.
- **It keeps the S0 proposal queue.** A plugin only proposes values. The
  host accepts or rejects each one with its *own* capability, exactly as
  with Z-Wasm. A worker has no capability that means anything to the host.

```c
ti_host_create(budget_cap, rmag, budget, "/opt/zxv/ti-worker", &host);
ti_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_PROPOSE);
ti_host_set_timeouts(host, 2000, 5000);           /* ms per call, ms to start + load */
ti_plugin_load(host, wasm, len, "fees", &plugin);  /* fork + exec a worker            */
ti_plugin_call(plugin, "fee", args, 2, &r, NULL, &fuel);
ti_queue_next(host, &id);
ti_proposal_accept(host, post_cap, id);           /* the host's cap, not the plugin's */
ti_plugin_destroy(plugin);                         /* QUIT, then SIGKILL if needed     */
```

Kit API version 0.1 (`include/triisolate.h`). It calls libzxv ABI 1.3 and
Z-Wasm kit API 0.2.

## What isolation this gives

**A process boundary, not a virtual machine.** Be clear about what that
means before you rely on it.

What the worker gets:

| | |
|---|---|
| Address space | Its own. A plugin that corrupts wasm3's memory corrupts the worker, not the host. |
| File descriptors | Only fd 3 (the socket to the host). stdin and stdout are `/dev/null`; stderr is `/dev/null` unless `TI_F_INHERIT_STDERR`. Everything else is closed before exec. |
| Environment | Empty. |
| Limits | `RLIMIT_CORE` 0 (no core dumps), `RLIMIT_NOFILE` 64, and on Linux `RLIMIT_AS` if you set `ti_host_set_memory_limit` (macOS does not enforce it, so it is refused there). |
| Signals | All handlers reset to default and the signal mask cleared before exec. On Linux, `PR_SET_PDEATHSIG` kills the worker if the host thread that started it exits. |
| Time | A wall-clock deadline per call and per load, enforced by the host with SIGKILL. |
| System calls | Unrestricted, unless you turn on the seccomp filter (Linux only, below). |

What it does **not** get you:

- **The worker runs as the same user as the host.** Without seccomp, code
  that escapes the Wasm sandbox inside the worker can do anything that
  user can: open files, connect to the network, start processes, signal
  other processes of the same user (including the host). The process
  boundary protects the host's *memory*, not the host's *user*.
- **It is not a container, a namespace or a VM.** There is no separate
  filesystem view, network namespace, user namespace or cgroup. If you
  need those, run the worker under a tool built for them (a container
  runtime, a VM, or a dedicated low-privilege user) and point
  `ti_host_create` at a wrapper that starts it there.
- **Fuel accounting trusts the worker's report, bounded above.** A worker
  can report less fuel than it used; it cannot report more than it was
  given (that is a protocol error, and the whole allotment is charged). The
  wall-clock timeout bounds what an under-reporting worker can take per
  call. If you need the strict bound, set `TI_F_CHARGE_ALLOTTED`: every
  call is then charged its whole allotment.
- **A worker's answers are values, not facts.** The host checks every frame
  strictly (lengths, types, status codes, normalised rationals, label
  sizes), but a compromised worker can still return a wrong result or
  propose a wrong value within those rules. That is why proposals are only
  proposals: the host decides, with its own capability. If you attach a
  z-contract, check results on the host side too.

### The optional seccomp filter (Linux)

`ti_host_set_flags(host, TI_F_SECCOMP)` makes each worker install a seccomp
filter after it has set up its libzxv context and Z-Wasm host and before it
reads the first byte of a plugin. From then on the worker may only:

- `read` fd 3, and `write` fd 3 or fd 2;
- `mmap` and `mprotect` without `PROT_EXEC` (wasm3 is an interpreter and
  never needs executable memory); `munmap`, `mremap`, `brk`, `madvise`;
- `close`, `futex`, `sched_yield`, `getrandom`, `clock_gettime`, `gettid`,
  `getpid`, `rt_sigreturn`, `rt_sigprocmask`, `sigaltstack`;
- `exit`, `exit_group`.

Any other system call kills the worker (`SECCOMP_RET_KILL_PROCESS`), and so
does a call through another architecture's table (x32 or i386 on x86-64).
That means no `open`, no sockets, no `execve`, no `fork`, no `kill`, no
`ptrace`. The host checks that the worker says it installed the filter
(`HELLO`), and refuses a worker that did not.

It is available on Linux x86-64 and arm64 (`ti_seccomp_available()`); on
other systems `TI_F_SECCOMP` gives `ZXV_E_NOTIMPL`. The tests run three
forbidden calls (`open`, `write` to fd 1, an executable `mmap`) with and
without the filter. Without it they succeed; with it each one kills the
worker with SIGSYS. The tests also run the real worker under the filter,
plain and under ASan + UBSan.

The filter narrows what an escaped plugin can do. It does not make the
worker a different user or hide the filesystem. Combine it with a
low-privilege user or a container if that matters to you.

### Platforms

Linux and macOS (POSIX `fork` + `execve`). **Windows is not supported yet.**
`ti_host.c` has stubs that return `ZXV_E_NOTIMPL` on Windows, but they are
not built or tested in CI.

## Quickstart

```sh
make -C ../../libzxv all     # once
make test                    # needs zig for Z-Wasm's example plugins (pip install ziglang)
make demo
```

`TI_SKIP_EXAMPLES=1 make test` runs without zig; the real-plugin tests are
then skipped and the hostile-worker tests still run.

`make demo` runs [`examples/isolate_demo.c`](examples/isolate_demo.c) on
Z-Wasm's example plugins:

```text
worker: build/ti-worker (with the seccomp filter)
load float plugin: ZW_E_REJECTED
  the host's validator refused the module: FLOAT_OP: f64.convert_i64_s (0xB9) in function 0: ...
rational plugin: running in worker process 5604
run(12) -> verdict TRUE, 706 fuel
proposal #1 from 'rational' kind 1: 12/13  "telescoping sum n/(n+1)"
  rejected by the host (not a postable integer amount)
proposal #2 from 'rational' kind 2: 600  "fee in minor units"
  accepted and posted as ledger entry 1
proposal #3 from 'rational' kind 2: 4.1625  "fee in minor units"
  rejected by the host (not a postable integer amount)
merchant balance: 600 minor units
spin(1) in process 5605: ZW_E_FUEL after 100000 fuel; worker killed by signal 9
budget used: 100830 of 1000000 compute units; 2 worker(s) started, 1 killed
```

## Using it

Build the worker (`make` gives `build/ti-worker`), install it next to your
host, and give `ti_host_create` its full path. The worker is started with
an empty environment, so it is not looked up in `PATH`.

| Call | What it does |
|---|---|
| `ti_host_create(budget_cap, rmag, budget, worker_path, &host)` | `budget_cap` needs `ZXV_CAP_BUDGET`; the budget pays for all calls |
| `ti_host_grant`, `ti_host_set_limit` | Z-Wasm grants and policy limits: used by the host's validator and sent to each worker |
| `ti_host_set_call_fuel` | most fuel per call (default 10^7) |
| `ti_host_set_timeouts(call_ms, load_ms)` | wall-clock limits (default 5000 and 10000 ms) |
| `ti_host_set_accept_caps` | the cap bits needed to decide a proposal (default `LEDGER_POST`) |
| `ti_host_set_memory_limit` | `RLIMIT_AS` for workers (default none; Linux only) |
| `ti_host_set_flags` | `TI_F_SECCOMP`, `TI_F_CHARGE_ALLOTTED`, `TI_F_INHERIT_STDERR` |
| `ti_plugin_load` / `_call` / `_destroy` | as Z-Wasm's, one worker per plugin |
| `ti_plugin_stop`, `ti_plugin_exit`, `ti_plugin_pid` | stop a worker and read how it ended |
| `ti_queue_*`, `ti_proposal_*` | the host-side S0 queue, as Z-Wasm's |

What a call can return:

| Status | Meaning | Plugin after |
|---|---|---|
| `ZXV_OK` | the result; proposals queued (`ZXV_E_CAPACITY` with the result written if they do not fit in 1024 pending; none is queued then) | READY |
| `ZW_E_EXPORT`, `ZXV_E_RANGE`, `ZXV_E_ARG` | the worker refused the call; nothing ran | READY |
| `ZXV_E_BUDGET` | no fuel left; nothing was sent | READY |
| `ZW_E_FUEL`, `ZW_E_TRAP` | the plugin stopped; the worker is killed | DEAD |
| `TI_E_TIMEOUT` (-1503) | past the call timeout; the worker is killed; the allotment is charged | DEAD |
| `TI_E_CRASHED` (-1504) | the worker died; `ti_last_error` names the signal or exit status | DEAD |
| `TI_E_PROTOCOL` (-1502) | the worker sent something malformed; it is killed | DEAD |
| `TI_E_DEAD` (-1505) | an earlier call stopped it | DEAD |

`ti_plugin_load` can also return `TI_E_SPAWN` (-1501): the worker could not
be started or exited before saying hello (a wrong path gives "exit status
127").

Rules for hosts:

- **One thread at a time** per host and its plugins, as with Z-Wasm.
- **Start workers from a long-lived thread on Linux.** `PR_SET_PDEATHSIG`
  fires when the *thread* that forked the worker exits, not the process.
- **Do not set `SIGCHLD` to `SIG_IGN`.** The host reaps its workers with
  `waitpid`. If they are reaped automatically, `ti_plugin_exit` reports
  `TI_EXIT_UNKNOWN`.
- **SIGPIPE is safe.** The host writes with `MSG_NOSIGNAL` (Linux) or
  `SO_NOSIGPIPE` (macOS), so a vanished worker gives an error, not a
  signal.

## Wire format

Host and worker talk over a connected Unix socket pair (a two-way pipe).
The worker's end is fd 3. Each message is one frame:

```text
u32le length   bytes that follow: 1 .. 4 MiB + 4 KiB (to the worker), 1 .. 16 KiB (to the host)
u8    type
...   body     length - 1 bytes, little-endian fields
```

| Type | Direction | Body |
|---|---|---|
| 1 HELLO | worker to host, first | `u16 version` (1), `u16 flags` (bit 0: seccomp installed) |
| 2 LOAD | host to worker, once | `u32 grants`, 15 x `u64` Z-Wasm limits, `u8 name_len` (<= 31, printable ASCII), name, `u32 wasm_len` (1 .. 4 MiB), wasm |
| 3 LOADED | worker to host | `i32 status` (OK, `ZW_E_REJECTED`, `ZW_E_LINK`, `ZXV_E_NOMEM`, `ZXV_E_ARG`), `u16 msg_len` (<= 512, printable ASCII), msg |
| 4 CALL | host to worker | `u64 fuel` (>= 1), `u8 export_len` (1 .. 128, no NUL), export, `u8 nargs` (<= 8), `nargs` x `i64` |
| 5 RESULT | worker to host | `i32 status`, `u8 has_result` (0/1), `i64 result`, `u64 fuel_used`, `u8 nprops` (<= 64), each `u32 kind, i64 num, i64 den, u8 label_len (<= 64), label`, `u16 msg_len`, msg |
| 6 QUIT | host to worker | empty |

The host refuses a RESULT, and kills the worker, if:

- the status is outside the set a call can give;
- it has a result or proposals with a non-OK status;
- it reports fuel for a call that never ran;
- `fuel_used` is more than the call was given;
- a proposal is not a normalised rational (`den > 0`, `gcd(|num|, den) = 1`);
- a label or the message is too long;
- any byte is left over.

The host also refuses any frame of the wrong type, and any length that is
0 or over the limit; it does not read the body of an oversized frame. The
decoders are shared by both sides (`src/ti_frame.c`), and the fuzzer checks
that every body they accept re-encodes to exactly the same bytes.

## Tests

| Test | What it covers | Checks |
|---|---|---|
| `test/test_frame.c` | every message round-trips (including extreme int64s); every truncation and every extended body is refused; every field rule on the encoder and the decoder; the largest RESULT fits the reply limit | 66 |
| `test/test_isolate.c` | API arguments; start-up failures (missing worker, exit before HELLO, wrong version, seccomp not installed, abort during LOAD, LOAD timeout, 900 KB sent to a vanished worker without SIGPIPE); what the worker inherits; 15 misbehaving workers (hang, slow drip, garbage, oversized and zero lengths, lying fuel, bad proposals, trailing bytes, wrong type, bad status, too many proposals, half a frame, a closed socket); after each, the whole allotment charged, nothing queued, the worker gone and no child left unreaped; the host-side queue (caps, decide-once, accept-and-post, the 1024 limit); budget and `TI_F_CHARGE_ALLOTTED`; a worker that ignores QUIT; seccomp (three forbidden calls with and without the filter); the real worker on Z-Wasm's plugins, compared call by call with in-process Z-Wasm (status, result, fuel and proposals), plain and under seccomp; fuel exhaustion, the wall-clock timeout, traps; a memory limit | 285 on Linux (280 under sanitizers, which skip the memory limit); fewer on macOS, which has no seccomp |
| `test/fuzz_frame.c` | deterministic mutation fuzzer over the decoders: only OK or FORMAT, accepted bodies are canonical, accepted RESULTs obey the host's rules | 5000 mutants in `make test` |
| `test/hostile_worker.c` | the misbehaving worker (one link per mode under `build/hw/`) | |
| `make check-nofloat` | no float type in the kit (R11), plus `-mgeneral-regs-only` where the compiler has it | |

`make fuzz FUZZ_ITERS=250000` runs longer, and `TI_FUZZ_SEED=n` picks
another stream. `make fuzz-libfuzzer` builds the same entry point with
libFuzzer and runs it for 60 seconds. CI runs `make test` with gcc and clang
on Linux and clang on macOS, and `SANITIZE=1` (ASan + UBSan, for the
library, the worker, the hostile worker and the tests) with four fuzz
seeds of 250,000 iterations each. Under sanitizers the real worker's own
leak check runs too: the tests require it to exit 0 on QUIT. Under
seccomp, the worker leaves with a bare `exit_group`, so the leak checker,
which needs `ptrace`, is skipped there.

## Files

| Path | What it is |
|---|---|
| `include/triisolate.h` | the public API |
| `src/ti_host.c` | starting, talking to, charging and killing workers; the S0 queue |
| `src/ti_frame.c`, `src/ti_frame.h` | the wire format (shared by host and worker) |
| `src/ti_seccomp.c` | the optional seccomp filter |
| `src/ti_wio.c` | blocking frame I/O for the worker side |
| `worker/ti_worker.c` | the worker program (`build/ti-worker`) |
| `test/` | frame tests, process tests, the hostile worker, the fuzzer |
| `examples/isolate_demo.c` | the demo |

Build output goes to `build/` (git-ignored). Z-Wasm is built into
`build/zwasm`; its own `build/` directory is not touched.

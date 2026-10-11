<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Z-Wasm: the ZXV plugin sandbox kit

Z-Wasm runs ZXV plugins: WebAssembly modules that a host loads, meters and
runs in a bounded sandbox, so third-party code can do integer and
exact-rational work for a ZXV application without touching its ledger.

A plugin goes through three stages:

1. **Validate.** A load-time validator written in C, with no float in its
   own code (rule R11), parses the binary and refuses anything outside the
   Z-Wasm profile. That covers floats of any kind, SIMD, memory or tables
   without a bounded maximum, imports the host did not grant, start
   functions, and anything over a size limit. Each refusal names the
   reason, the section, the function index and the byte offset.
2. **Run.** The module is fuel-metered and run in a vendored copy of
   [wasm3](THIRD_PARTY.md), built with its float support compiled out.
   Each call's fuel is paid from a libzxv `rmag` budget, exactly. A trap or
   running out of fuel stops the plugin and is reported. The host's
   `zxv_ctx` is never affected.
3. **Propose (the Tri-Isolate starter).** A plugin can only call the host
   functions the host granted. To produce a result it *proposes* an exact
   rational into a host-side queue, where it waits in state S0 (neutral,
   `ZXV_LP_NEITHER`). Nothing reaches the ledger until the host accepts the
   proposal with its own capability.

> **What the isolation is, and is not.** The isolation comes from the Wasm
> runtime (memory safety, no ambient authority) and from capabilities
> (least privilege). It is **not** a proof that a plugin computes the right
> thing. The host must still judge every proposal. See
> [Threat model](#threat-model).

Kit API version 0.2 (`include/zwasm.h`; 0.2 added the call-check hook). It may still change before 1.0.
It calls the libzxv ABI 1.2 (`sdk/libzxv/include/zxv_api.h`), which is
frozen as usual.

## Quickstart

```sh
make -C sdk/libzxv                      # the C library every SDK wraps
python3 -m pip install ziglang          # wasm32 compiler for the example plugins
make -C sdk/kits/z-wasm test demo       # tests, then the host demo
```

The demo loads the float plugin, which is refused, and runs the rational
plugin. It accepts the integer fee proposal, posts it to the ledger with
the host's capability, and rejects the fractions. It then stops a runaway
plugin when its fuel runs out:

```text
load build/examples/float_plugin.wasm: ZW_E_REJECTED
  FLOAT_OP: f64.convert_i64_s (0xB9) in function 0: Z-Wasm plugins use integers and exact rationals (zxv.rat_*), never floats [code section, function 0, offset 81]
run(12) -> verdict TRUE, 706 fuel
proposal #1 from 'rational' kind 1: 12/13  "telescoping sum n/(n+1)"
  rejected by the host (not a postable integer amount)
proposal #2 from 'rational' kind 2: 600  "fee in minor units"
  accepted and posted as ledger entry 1
proposal #3 from 'rational' kind 2: 4.1625  "fee in minor units"
  rejected by the host (not a postable integer amount)
merchant balance: 600 minor units
spin(1): ZW_E_FUEL after 100000 fuel: plugin 'spin' ran out of fuel after 100000 units in 'spin'
budget used: 100830 of 1000000 compute units
ledger check: OK
```

### A host in a few lines

```c
#include "zwasm.h"

zxv_cap *budget_cap, *post_cap;   /* attenuated from your root cap      */
zxv_rmag *rm; uint32_t fuel;      /* an ENFORCED compute budget         */
zw_host *host;
zw_plugin *pl;

zw_host_create(budget_cap, rm, fuel, &host);         /* needs ZXV_CAP_BUDGET */
zw_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_PROPOSE);
if (zw_plugin_load(host, wasm, wasm_len, "fees", &pl) == ZW_E_REJECTED)
    /* zw_last_error says why, where (section, function, offset) */;
int64_t args[2] = {48000, 125};
int32_t st = zw_plugin_call(pl, "fee", args, 2, NULL, NULL, NULL);
uint64_t id;
while (zw_queue_next(host, &id) == ZXV_OK)          /* S0 proposals         */
    zw_proposal_accept_post(host, post_cap, id, from, to, "NGN", "ref-1", NULL);
```

### Writing a plugin

A plugin is C, Zig, Rust or anything else that compiles to `wasm32` with
integer code only. In C, include
[`examples/plugins/zxv_plugin.h`](examples/plugins/zxv_plugin.h) and build
with:

```sh
python3 -m ziglang cc --target=wasm32-freestanding -O2 -nostdlib -mno-bulk-memory \
  -Wl,--no-entry -Wl,-z,stack-size=16384 \
  -Wl,--initial-memory=131072 -Wl,--max-memory=131072 plugin.c -o plugin.wasm
build/zw-tool validate --grant rational,propose plugin.wasm
```

`--max-memory` is required, because a memory without a maximum is refused.
`-mno-bulk-memory` keeps `memcpy` from emitting `memory.copy`, which the
default policy refuses (see [Limits](#limits)). Export functions with
`__attribute__((export_name("...")))`. Parameters and results must be
`i32`/`i64`, with at most 8 parameters and 1 result.

## The validator (`zw_validate`)

The validator parses every section of the WebAssembly core 2.0 binary
format and every instruction of every function body. Any of the following
is a refusal, with a `ZW_R_*` reason:

| Reason | Refused |
|---|---|
| `FLOAT_TYPE` | an `f32`/`f64` value type anywhere: function types, locals, globals, global imports, block types, `select t` |
| `FLOAT_OP` | every float instruction: `0x2A`-`0x2B`, `0x38`-`0x39`, `0x43`-`0x44`, `0x5B`-`0x66`, `0x8B`-`0xBF` except the integer `0xA7` `i32.wrap_i64`, `0xAC`/`0xAD` `i64.extend_i32_s/u`, and the saturating truncations `0xFC 0x00`-`0x07`. That is 76 opcodes, also refused inside constant expressions |
| `SIMD` | the `v128` type and every `0xFD`-prefixed instruction |
| `MEMORY_NO_MAX`, `MEMORY_LIMIT` | a memory without a maximum, or a maximum above `ZW_LIM_MEMORY_PAGES` (default 16 pages, 1 MiB) |
| `TABLE_LIMIT` | a table without a maximum, above `ZW_LIM_TABLE_ELEMS` (1024), or more than one table |
| `IMPORT_DENIED`, `IMPORT_SIG`, `IMPORT_KIND` | a function import not on the allow-list, an allowed name with another signature, and any table, memory or global import |
| `START` | a start function, which would run before the host sets the fuel |
| `TOO_LARGE`, `LIMIT` | module bytes, functions, body bytes, locals, globals, types, imports, exports, data bytes, element count, block nesting and parameters (all set with `zw_policy_set_limit`) |
| `RESERVED_NAME` | an export starting with `__zw_` (the kit's fuel export) |
| `UNSUPPORTED` | shared memory, `memory64`, multiple memories, exception handling, tail calls, GC, atomics, and bulk memory unless `ZW_LIM_BULK_MEMORY` is 1 |
| `INDEX` | an out-of-range type, function, local, global, table, memory, data, element or label index |
| `MALFORMED` | everything else that is not a well-formed binary: bad magic or version, section order, non-canonical LEB128, invalid UTF-8 names, duplicate exports, unbalanced blocks, and so on |

Example message:

```text
FLOAT_OP: f32.convert_i32_s (0xB2) in function 2: Z-Wasm plugins use integers and
exact rationals (zxv.rat_*), never floats [code section, function 2, offset 51]
```

The function index is in the module's function index space, imports first,
as `wasm-objdump` and browsers number functions. The offset is the byte
offset of the offending byte in the file.

The validator is a **profile filter plus a structural decoder**. It checks
block structure and bounds-checks every index, but it does not type-check
the operand stack. wasm3's own load-time validator does that, and a type
error surfaces as `ZW_E_LINK` from `zw_plugin_load`.

## The host (`zw_host`, `zw_plugin`)

| Call | What it does |
|---|---|
| `zw_host_create(budget_cap, rmag, budget, &host)` | The cap must hold `ZXV_CAP_BUDGET` and belong to the rmag's context. The host keeps both pointers. |
| `zw_host_grant(host, ZW_GRANT_*)` | Which host-function groups plugins may import. Nothing else can be imported. |
| `zw_host_policy(host, &policy)` | The limits applied to plugins this host loads. |
| `zw_host_set_call_fuel`, `zw_host_set_stack_bytes`, `zw_host_set_accept_caps` | Per-call fuel ceiling (default 10^7), wasm3 stack (64 KiB), and the capability bits needed to decide proposals (default `ZXV_CAP_LEDGER_POST`). |
| `zw_plugin_load(host, wasm, len, name, &pl)` | Validate, meter, re-validate the metered module, then parse, link and compile with wasm3. |
| `zw_plugin_call(pl, export, args, n, &result, &has_result, &fuel_used)` | Run one exported function. |
| `zw_plugin_state`, `zw_plugin_destroy` | `ZW_PLUGIN_READY` or `ZW_PLUGIN_DEAD`. |
| `zw_plugin_set_check(pl, export, fn, user)` | Attach a call check to one export (0.2). See [Call checks](#call-checks-kit-api-02). |

**Fuel.** The meter injects a mutable `i64` global, exported as `__zw_fuel`,
and charges it at the start of every basic block: one unit per
instruction, before the block runs. A block whose charge drives the
counter below zero executes `unreachable`. Blocks start at function entry
and after every `loop`, `if`, `else`, `end`, `br`, `br_if`, `br_table`,
`return` and `unreachable`. Every branch target is such a start, so no
instruction runs unpaid. Each host-function call costs a further
`ZW_HOST_CALL_FUEL` (16).

For each call the fuel is `min(call fuel, floor(remaining budget))`. Once
the call returns, the exact amount used is consumed from the budget with
`zxv_rmag_consume`. The tests check this to the unit: a 3-instruction
function costs 4, counting its `end`. If the budget is empty, the call
returns `ZXV_E_BUDGET` and nothing runs.

The plugin's own code cannot reach the fuel counter. The validator bounds
every global index by the module's original global count, and the new
global comes after them. Only the host sees it.

**Traps.** A trap (`ZW_E_TRAP`) or fuel exhaustion (`ZW_E_FUEL`) marks the
plugin DEAD, because its memory may be half-written. Destroy it and load a
fresh one. The host's `zxv_ctx` is never touched. If the host wants a
plugin failure to halt the platform, it calls `zxv_ctx_halt` itself.
Conversely, a halted (poisoned) context refuses plugin calls
(`ZXV_E_POISONED`), because the budget capability no longer verifies.

### Call checks (kit API 0.2)

A small integration hook, added for [z-contract](../z-contract/), which
enforces a typed contract with exact bounds on an export.
`zw_plugin_set_check(pl, export, fn, user)` attaches one check to one
export (at most `ZW_CHECKS_MAX`, 16, per plugin). Every `zw_plugin_call` of
that export then runs it twice:

| Stage | When | A non-zero return |
|---|---|---|
| `ZW_CHECK_BEFORE` | after the arguments fit the export's types, before any fuel is set | refuses the call with `ZW_E_CHECK`. Nothing runs, no fuel is used, and the plugin stays READY, because bad input is the caller's fault. |
| `ZW_CHECK_AFTER` | after a normal return and the fuel charge, before staged proposals reach the queue | is handled like a trap. The staged proposals are discarded, the plugin becomes DEAD, and the call returns `ZW_E_CHECK`. |

During `ZW_CHECK_AFTER`, `zw_plugin_staged_count` and
`zw_plugin_staged_get` read the proposals the call staged, so a check can
judge a plugin's real outputs before the host sees them. A check cannot
call the plugin or change checks from inside itself (`ZXV_E_STATE`). The
check is host code: it runs in the host's process with the host's
privileges, like any callback.

### Host functions (import module `zxv`)

| Grant | Import | wasm3 signature | Meaning |
|---|---|---|---|
| `ZW_GRANT_RATIONAL` | `rat_add`, `rat_sub`, `rat_mul`, `rat_div` | `i(IIIIi)` | `(an, ad, bn, bd, out*)`: an exact result written to `out` as two little-endian `i64`, returns a ZXV status |
| | `rat_cmp` | `i(IIIIi)` | writes −1/0/+1 as `i32` to `out` |
| | `rat_normalize` | `i(IIi)` | `(num, den, out*)` |
| `ZW_GRANT_LOGIC` | `lp_not` | `i(i)` | four-valued logic, returns 0..3 or a negative status |
| | `lp_and`, `lp_or` | `i(ii)` | |
| | `lp_from_evidence` | `i(ii)` | `(positive, negative)` counts |
| `ZW_GRANT_PROPOSE` | `propose` | `i(iIIii)` | `(kind, num, den, label*, label_len <= 64)`: stage an exact rational for the S0 queue |

These are libzxv's pure functions (`zxv_rat_*`, `zxv_lp_*`) and the
host-side queue. No plugin ever holds a `zxv_cap`, a context, a key or a
ledger handle. Out-of-bounds pointers return `ZXV_E_ARG` to the plugin.

## Tri-Isolate starter: the S0 queue

```text
 plugin (Wasm, fuel-bounded)        host (C, holds the capabilities)
 ───────────────────────────        ─────────────────────────────────
 zxv.propose(kind, n, d, label) ──► staged for this call
                    call returns ──► queue: state S0 (ZXV_LP_NEITHER)
                    call traps   ──► staged proposals discarded
                                     zw_proposal_accept(host, cap, id)      -> TRUE
                                     zw_proposal_reject(host, cap, id)      -> FALSE
                                     zw_proposal_accept_post(host, cap, id, from, to, ccy, ref)
                                       posts with zxv_ledger_post_cap; TRUE only if posted
```

- Proposals reach the queue only when the call returns normally. A plugin
  that proposes and then traps leaves nothing behind.
- Deciding a proposal needs a capability that passes `zxv_cap_check` for
  the host's accept caps (default `ZXV_CAP_LEDGER_POST`). A read-only cap
  gets `ZXV_E_CAP`, and the proposal stays S0. A proposal is decided once
  (`ZXV_E_STATE` after that).
- `accept_post` posts only a positive integer proposal (`den == 1`) as
  minor units. A refused post (funds, currency, capability) leaves the
  proposal in S0 and returns the libzxv status.
- At most 1024 proposals are pending at once. Past that, `propose` returns
  `ZXV_E_CAPACITY` to the plugin. Ids never repeat. Decided proposals are
  kept until 4096 have accumulated, then the oldest decided ones are
  evicted.
- `kind`, the value and the label are **untrusted plugin data**. The queue
  isolates *when* and *by whom* a result takes effect, not *whether it is
  right*.

## Threat model

**Assets.** The host's ledger, keys and capabilities, its process memory,
and its CPU and memory budget.

**Adversary.** A plugin author who controls every byte of the `.wasm`
file.

| Protection | Mechanism |
|---|---|
| No ambient authority | No WASI, no files, network, clock or randomness. The only imports are the granted `zxv.*` functions, checked by name and exact signature at load. |
| Memory safety | wasm3 bounds-checks every linear-memory access (it traps). Host functions bounds-check pointers. |
| Bounded CPU | The fuel meter charges every basic block from an rmag budget, and the host sets a per-call ceiling. |
| Bounded memory | A declared memory maximum is required and capped by the policy, and `memory.grow` past it fails inside Wasm. Tables are capped too. The wasm3 value stack is fixed per plugin, and its native stack budget turns runaway recursion into a trap. |
| Bounded load cost | Module, body, locals, globals, types, data and element counts are all limited before wasm3 sees the module. |
| No float nondeterminism | Every float and SIMD type and instruction is refused, and wasm3 is built without float support (`d_m3HasFloat=0`). |
| Least privilege for results | Plugins propose; only a capability holder accepts. |
| Failure containment | A trap or fuel stop kills the plugin and discards its staged proposals. It never poisons the host context. |

**Out of scope, and the honest limits:**

- **Correctness.** Nothing here proves a plugin computes the right answer.
  The host decides on each proposal.
- **The interpreter is in the trusted computing base.** wasm3 runs in the
  host's process. A memory-safety bug in wasm3 would be a host compromise.
  We fuzz it through the kit (see [Tests](#tests)), but for hostile code
  the next step is a process boundary: plugins in their own process, as
  `zxvd` will do for capabilities.
- **The validator is not a full Wasm validator.** It does not type-check
  the operand stack; wasm3 does that at load.
- **Fuel is instruction count, not time.** Different instructions cost
  different amounts of real time. Host calls cost a flat 16 units, and
  `memory.grow` costs 1.
- **Bulk memory is off by default.** One `memory.fill` or `memory.copy`
  costs 1 fuel unit whatever its length. With `ZW_LIM_BULK_MEMORY` set to
  1, a single instruction can do up to a memory's worth of work, so keep
  memory small if you enable it.
- **Side channels** (timing, cache) are not addressed.
- **Capabilities are in-process least privilege**, exactly as libzxv
  documents. Host code in the same process can make its own caps.
- **Threads.** A `zw_host` and its plugins are not thread-safe. Use them
  from one thread at a time. Destroy every plugin before its host.
- **The kit API is 0.x.** It is not yet covered by the libzxv stability
  promise.

## Limits

| Limit (`ZW_LIM_*`) | Default | Max |
|---|---|---|
| `MODULE_BYTES` | 1 MiB | 64 MiB |
| `MEMORY_PAGES` | 16 (1 MiB) | 65536 |
| `TABLE_ELEMS` | 1024 | 2^20 |
| `FUNCTIONS` | 4096 | 100000 |
| `BODY_BYTES` | 128 KiB | 8 MiB |
| `LOCALS` (per function) | 1024 | 50000 |
| `GLOBALS` | 256 | 4096 |
| `TYPES` | 1024 | 100000 |
| `IMPORTS` | 64 | 1024 |
| `EXPORTS` | 256 | 4096 |
| `DATA_BYTES` | 1 MiB | 64 MiB |
| `NESTING` | 256 | 990 |
| `PARAMS` (per type) | 16 | 16 |
| `BULK_MEMORY` | 0 (off) | 1 |
| `ELEM_SEGMENTS` (elements) | 4096 | 2^20 |

Other fixed limits: one memory, one table, at most 8 call arguments and 1
result, a 64-byte label, 64 proposals per call and 1024 pending.

## Tests

```sh
make test                          # everything below except the long runs
make SANITIZE=1 OUT=build/san test # the same under ASan + UBSan (gcc)
make fuzz FUZZ_ITERS=500000        # deterministic mutation fuzzer
ZW_FUZZ_SEED=3 make fuzz           # another deterministic stream
make fuzz-libfuzzer                # libFuzzer, if clang ships its runtime
make test-node                     # metered modules re-validated by V8
```

| Suite | What |
|---|---|
| `test_validate` | 1140 checks: all 256 opcodes and the 0xFC sub-opcodes classified against the spec list, float and SIMD types in every position, memory, table, import, start and size limits, index checks, malformed binaries, LEB128 and UTF-8 edge cases, exact error location, and the meter's output passing the profile again |
| `test_host` | 149 checks: exact fuel accounting against the rmag budget, fuel and budget exhaustion, traps (unreachable, divide by zero, out-of-bounds, stack overflow), grants, i32 argument ranges, the S0 queue (caps, decide-once, capacity, discard on trap), call checks (refuse before, kill after, staged proposals discarded, no re-entry), `accept_post` against the ledger, fail-closed on a halted context, and the three example plugins |
| `fuzz_validate` | for every input: `zw_validate` is deterministic, reports an in-range reason and offset and terminates its message; accepted modules meter and re-validate, load into wasm3 (or get `ZW_E_LINK`), and every export runs to a result, a trap or out of fuel without poisoning the host |
| `node_crosscheck.mjs` | V8's `WebAssembly.validate` accepts every metered module whose input it accepts (the examples plus 300 mutants each, and the fuzz seeds) |
| `check-nofloat` | R11: no float type in the validator, meter or policy sources (comments and strings excluded), plus `-mgeneral-regs-only` where the compiler enforces it |
| `verify-third-party` | the vendored wasm3 is byte-identical to the checksums in `THIRD_PARTY.md` |

The example `.wasm` files are compiled at test time from
`examples/plugins/*.c`. No binary is committed. Run with
`ZW_SKIP_EXAMPLES=1` to test without zig.

## Files

| Path | |
|---|---|
| `include/zwasm.h` | the kit API |
| `src/zw_validate.c`, `src/zw_wasm.c`, `src/zw_meter.c`, `src/zw_policy.c` | validator, decoder, fuel meter, policy (R11: integer code only) |
| `src/zw_host.c` | wasm3 host, host functions, S0 queue |
| `third_party/wasm3/` | wasm3 v0.9.0 (MIT), unmodified, see [THIRD_PARTY.md](THIRD_PARTY.md) |
| `examples/plugins/` | `rational_plugin.c` (exact work, proposes), `float_plugin.c` (refused), `spin_plugin.c` (misbehaves), `zxv_plugin.h` |
| `examples/host_demo.c` | the demo above |
| `tools/zw_tool.c` | `zw-tool validate` / `zw-tool meter` |

## Licence

Apache-2.0, except `third_party/wasm3` (MIT, see
[THIRD_PARTY.md](THIRD_PARTY.md)). Registered to 36N9 Genetics LLC, Michael
Laurence Curzi. Provided without warranty.

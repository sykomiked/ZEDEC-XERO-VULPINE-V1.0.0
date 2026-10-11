<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The libzxv C ABI

Every ZXV SDK is a thin layer over one C library, `libzxv`, declared in
[`sdk/libzxv/include/zxv_api.h`](../libzxv/include/zxv_api.h). This page
explains the rules in that header with small examples. If you write a new
binding, read this page and [SDK_DESIGN.md](SDK_DESIGN.md); if you only use
an SDK, you can skip it.

ABI version: **1.3.0**. Check it at load time with `zxv_version`.

## Building it

```sh
make -C sdk/libzxv            # build/libzxv.so (or .dylib) and build/libzxv.a
make -C sdk/libzxv test       # 3,478 C conformance checks + the portable Q16 sweep
make -C sdk/libzxv SANITIZE=1 test   # the same under ASan + UBSan
make -C sdk/libzxv fixtures   # sdk/conformance/fixtures: tiny test model
make -C sdk/libzxv dist       # build/dist/{include,lib} for packaging
```

Options: `ZXV_NO_PQC=1` (no ML-KEM, ML-DSA, Ed25519), `ZXV_NO_TENSOR=1` (no
inference engine), `ZXV_NO_CBANK=1` (no ISO 4217 table). Calls into a
left-out group return `ZXV_E_NOTIMPL`, and `zxv_features` says so.

## The rules

### R1 Opaque handles

You only ever hold pointers to incomplete types (`zxv_ctx`, `zxv_cap`,
`zxv_model`, ...). No struct layout crosses the boundary, so bindings never
break when the kernel changes a struct.

### R2 Scalars only

Parameters are fixed-width integers, `size_t`, UTF-8 strings, byte buffers,
or out pointers to those. Money is exact `int64` minor units.

### R3 Status codes

Almost every function returns `int32_t`: `ZXV_OK` (0) or a negative
`ZXV_E_*`. Out parameters are written only on success.

```c
int64_t equity;
int32_t rc = zxv_ledger_balance_cap(cap, account, NULL, NULL, &equity);
if (rc != ZXV_OK) {
    char why[256]; size_t n;
    zxv_last_error(why, sizeof why, &n);
    fprintf(stderr, "%s: %s\n", zxv_status_name(rc), why);
}
```

### R4 Two-call buffers

Functions that return text take `(buf, cap, *out_len)`. Call once with
`NULL` to learn the size, then again with a buffer one byte larger (room
for the NUL).

```c
size_t need;
zxv_rat_format(1, 3, NULL, 0, &need);        /* ZXV_E_BUFFER, need = 3 */
char *s = malloc(need + 1);
zxv_rat_format(1, 3, s, need + 1, &need);    /* "1/3" */
```

Two calls return arrays, not text, and need no `+ 1`:

- `zxv_tokenize` fills an `int32_t` array with no terminator. `*out_count`
  is the exact number of ids, and `cap >= *out_count` is enough.
- `zxv_evm_encode_state` and `zxv_evm_ledger_state` write raw bytes with no
  NUL. `*out_len` is exactly `(6 + 2 * count) * 32`, and
  `cap_bytes >= *out_len` is enough. The digest is written only on `ZXV_OK`.

One out parameter is written on a failure status: `zxv_swarm_consume`
sets `*out_granted` on `ZXV_E_BUDGET` as well, because the partial grant
was consumed. Read it before you treat `BUDGET` as a failure.

### R5 UTF-8

Strings in and out are UTF-8. Invalid input is `ZXV_E_UTF8`.

### R6 Threading

A `zxv_ctx` is guarded by a mutex: share it across threads freely. Other
handles are documented per group. Destroy each handle once, after every
call on it has returned, and destroy children before parents:

| Destroy first | before |
|---|---|
| `zxv_session` | its `zxv_model`, and any `zxv_rmag` bound to it |
| `zxv_msg` made with `zxv_msg_create_cap` | that `zxv_cap` |
| an attenuated `zxv_cap` | its parent cap |
| `zxv_cap`, `zxv_rmag`, `zxv_phase`, `zxv_lpres`, `zxv_swarm`, `zxv_msg`, `zxv_hexlog` | their `zxv_ctx` |

A `zxv_model` holds no context and may outlive the cap that opened it.

`zxv_session_bind_budget` keeps the `zxv_rmag` pointer, not a copy. Keep
that rmag, and its context, alive until you unbind (`rmag = NULL`),
rebind or destroy the session.

### R7 Last error

`zxv_last_error` gives thread-local diagnostic text. Branch on the status
code, never on the text.

### R8 Versioning

The major version changes only on a breaking change. New functions, enum
values and status codes are minor. Treat an unknown negative status as an
error of class SYSTEM.

### R9 Features

`zxv_features()` returns a bitmask of `ZXV_FEAT_*` groups live in this
build. `ZXV_FEAT_LORA` is never set yet.

### R10 Capabilities (new in 1.1)

Every call that touches state, keys or the engine takes a `const zxv_cap *`
first and checks it on every call: the handle and all its ancestors are not
revoked, the needed bit is set, and the attestation is `ZXV_LP_TRUE`.

Since 1.2, a cap passed with a handle (rmag, phase, lpres, swarm, or the
budget bound to a session) must also belong to the same context as that
handle. A cap from another context gets `ZXV_E_CAP`.

Revocation propagates to children; attestation does not. `zxv_cap_attenuate`
copies the parent's attestation at that moment, and a later
`zxv_cap_set_attestation` on the parent leaves existing children as they
are. To stop every child at once, revoke the parent.

```c
zxv_cap *root, *reader;
zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root);
zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader);  /* narrow only */
zxv_ledger_post_cap(reader, a, b, 100, "NGN", "ref-1", &id);  /* ZXV_E_CAP */
zxv_cap_revoke(root);               /* reader is now dead too */
```

| Bit | Allows |
|---|---|
| `LEDGER_READ` | balances, journals |
| `LEDGER_POST` | postings |
| `CRYPTO_SIGN` | ML-DSA-87, Ed25519 signing |
| `CRYPTO_KEM` | ML-KEM-1024 |
| `AI_INFERENCE` | model open, generate |
| `BUDGET` | rmag budgets |
| `PHASE` | phase clock admit, defer, advance |
| `ATTEST` | lpres attestations; the quarantine log (`zxv_hexlog_create`, `zxv_hexlog_append`, 1.3) |
| `SWARM` | swarm token economy |
| `EVM` | state-channel encoding |

Pure functions (rationals, logic tables, the hexagram tag, hashes, wire
framing) take no capability.

ABI 1.2 adds `_cap` forms for the 1.0 context calls that took none, and for
the 1.1 handle creates. The old forms keep working unchanged (the .NET
finance SDK and other 1.0 bindings use them) and are deprecated:

| Deprecated | Use | Needs |
|---|---|---|
| `zxv_ccy_enable`, `zxv_ccy_enable_with_minor`, `zxv_ccy_register_private` | the same name + `_cap` | `LEDGER_POST` |
| `zxv_ledger_open_account` | `zxv_ledger_open_account_cap` | `LEDGER_POST` |
| `zxv_ledger_account_{ccy,name,kind,count}`, `zxv_ledger_entry_{count,at}` | the same name + `_cap` | `LEDGER_READ` |
| `zxv_msg_create` | `zxv_msg_create_cap` (the message re-checks its cap on every `zxv_msg_*` call) | `LEDGER_READ` |
| `zxv_rmag_create`, `zxv_phase_create`, `zxv_lpres_create`, `zxv_swarm_create` | the same name + `_cap` | `BUDGET`, `PHASE`, `ATTEST`, `SWARM` |

Honest limit: inside one process this is least-privilege discipline, not a
sandbox. Code in the same address space can create its own root
capability. It becomes enforcement at a process boundary (the `zxvd`
daemon over IPC).

### R11 No floats (new in 1.1)

No parameter is `float` or `double`. Exact values cross as two `int64`
(numerator, denominator). Engine parameters are Q16.16 in an `int32`
(value = q / 65536). A host double enters only through
`zxv_rat_from_ieee754(bits)`, which takes the raw IEEE-754 bits and returns
the exact rational they denote: 0.1 becomes
3602879701896397/36028797018963968. NaN and infinity are `ZXV_E_FLOAT`.

## Status classes

`zxv_status_class(rc)` sorts every code into one of five classes, and every
SDK maps them to the same exception families.

| Class | Codes (examples) | What to do |
|---|---|---|
| OK (0) | `ZXV_OK` | |
| USAGE (1) | `ARG`, `RANGE`, `UTF8`, `FORMAT`, `FLOAT`, `RMAG_INVALID` | fix the call |
| REFUSED (2) | `CAP`, `FUNDS`, `BUDGET`, `VERIFY`, `NOTIMPL`, `STATE`, `DECLINED` | handle it; may succeed later |
| FATAL (3) | `INVARIANT`, `POISONED` | stop; never retry |
| SYSTEM (4) | `NOMEM`, `IO`, `INTERNAL`, unknown | environment |

## Fail-closed

When the ledger finds a broken invariant it rolls back and **poisons** the
context: every later call on it, or on any handle made from it (caps,
messages, rmag, phase, lpres, swarm, quarantine logs, and the netting, card
and VSS stubs),
returns `ZXV_E_POISONED`, whatever the host does. The check comes before
argument validation, so a bad argument still gets `POISONED`. Only the
destroy functions, `zxv_ctx_is_poisoned` and `zxv_cap_revoke` (which can
only take authority away) still work. `zxv_ctx_is_poisoned` reports it and `zxv_ctx_halt(ctx, reason)`
triggers it on purpose (for tests, or when the host detects something
wrong). A C library cannot make a host exception uncatchable; poisoning is
what makes retrying impossible. SDKs then raise a terminal error type and
offer an opt-in policy that aborts the process.

## Groups added in 1.1

| Group | Prefix | Kernel module |
|---|---|---|
| Exact rationals | `zxv_rat_` | `kernel/src/rational` |
| Four-valued logic | `zxv_lp_`, `zxv_lpres_` | `kernel/src/lpres` |
| Capabilities | `zxv_cap_` | `kernel/src/sdk_bridge` model |
| Resource budgets | `zxv_rmag_` | `kernel/src/rmag` |
| Phase clock | `zxv_phase_` | `kernel/src/phase_coord` |
| UBH-168 frames | `zxv_wire_` | `kernel/src/harmonic` |
| Swarm economy | `zxv_swarm_` | `kernel/src/swarm` |
| Hashes | `zxv_sha3_256`, `zxv_shake256`, `zxv_keccak256` | `kernel/src/mlkem/keccak` + own Keccak-256 |
| Post-quantum crypto | `zxv_mlkem1024_`, `zxv_mldsa87_` | `kernel/src/pqsec` |
| Ed25519 | `zxv_ed25519_` | `kernel/third_party/ed25519` |
| Inference engine | `zxv_model_`, `zxv_session_`, `zxv_generate` | `kernel/src/tensor` |
| EVM state channels | `zxv_evm_` | own ABI encoder |

## Changes in 1.2

- Fail-closed covers every entry point and every child handle (see above).
- Caps are bound to their context for rmag, phase, lpres, swarm and a
  session's bound budget.
- The `_cap` forms in the table above.
- `zxv_rmag_listen` waits for listener calls running on other threads, so
  once it returns the old `user` pointer can be freed. A listener that
  calls the library no longer overwrites the caller's `zxv_last_error`.
- `zxv_random` uses `BCryptGenRandom` on Windows (link `bcrypt`) and
  `getentropy` on WASI. `zxv_rat_to_q16` no longer needs `__int128`.
- `zxv_wire_pack` refuses a tag above 199 with `ZXV_E_ARG`. A tag is
  `100*sync + 10*shell + trunk`, so 0..199 are the only valid ones.
- Phase event ids are a 64-bit counter and are never reused, so a stale id
  cannot cancel a newer event. A finished event's slot is recycled once
  4096 newer ids exist, and its old id then gives `ZXV_E_NOT_FOUND`.
- `zxv_session_position` after `zxv_generate` on a fresh session is
  `prompt + N - 1`. The last generated token is returned but not yet in
  the KV cache.

## Changes in 1.3

1.3 adds the kernel's 64-state paraconsistent hexagram tag
(`kernel/src/lpres/lpres_hex.h`, the source of truth) and its quarantine
log. Feature bit: `ZXV_FEAT_HEX` (`0x08000000`). Nothing in 1.2 changed.

### The tag

One byte, `tag = base << 4 | polarity << 2 | phase`, 0..63. Every function
takes and returns fixed-width integers, and every tag, base, polarity or
phase argument above its range is refused with `ZXV_E_ARG` (a tag above 63,
a field above 3).

| Field | Values |
|---|---|
| base (bits 5-4) | `ZXV_HEX_BASE_FALSE` 0, `NEITHER` 1, `BOTH` 2, `TRUE` 3 |
| polarity (bits 3-2) | `ZXV_HEX_POL_ENTROPIC` 0, `NEUTRAL` 1, `FORBIDDEN` 2, `NEGENTROPIC` 3 |
| phase (bits 1-0) | `ZXV_HEX_PHASE_POTENTIAL` 0, `EMERGING` 1, `DISSOLVING` 2, `MANIFEST` 3 |
| action | `ZXV_HEX_ACT_PROCEED` 0, `WAIT` 1, `REFUSE` 2, `ESCALATE` 3, `ROLLBACK` 4 |

**The base is not numbered like `ZXV_LP_*`.** The base is numbered by its
line pattern (F 00, N 01, B 10, T 11); the four-valued logic numbers
NEITHER 0, TRUE 1, FALSE 2, BOTH 3. Never cast one to the other; convert
with `zxv_hex_from_lp` and `zxv_hex_to_lp`:

| State | `ZXV_LP_*` | `ZXV_HEX_BASE_*` |
|---|---|---|
| NEITHER | 0 | 1 |
| TRUE | 1 | 3 |
| FALSE | 2 | 0 |
| BOTH | 3 | 2 |

`zxv_hex_from_lp` refuses a value outside 0..3 with `ZXV_E_ARG` (the
kernel's own helper maps it to NEITHER). Every SDK exposes this only as an
explicit conversion and tests the table against `vectors.json`.

| Function | Does |
|---|---|
| `zxv_hex_pack`, `zxv_hex_unpack` | fields <-> tag (`unpack` out pointers may be NULL, so it doubles as a range check) |
| `zxv_hex_action` | the kernel's 64-entry action table: FORBIDDEN -> ROLLBACK; TRUE -> PROCEED (WAIT if entropic); FALSE -> REFUSE; BOTH -> WAIT if negentropic, else ESCALATE; NEITHER -> WAIT |
| `zxv_hex_phase_next` | the cycle POTENTIAL -> EMERGING -> MANIFEST -> DISSOLVING -> POTENTIAL (a Gray code: one bit per step) |
| `zxv_hex_step` | move to the same or the next phase; a jump is `ZXV_E_STATE` |
| `zxv_hex_set` | new base and polarity, same phase; a forbidden tag asked for another polarity is `ZXV_E_STATE` (the kernel's `lpx_set` silently keeps it forbidden; the ABI refuses instead, so a caller cannot believe it un-forbade something) |
| `zxv_hex_forbid` | polarity FORBIDDEN, base and phase kept |
| `zxv_hex_and`, `zxv_hex_or`, `zxv_hex_not` | base by `zxv_lp_and` / `zxv_lp_or` / `zxv_lp_not`; polarity the worst of the two (FORBIDDEN > ENTROPIC > NEUTRAL > NEGENTROPIC); phase the least advanced on the cycle order POTENTIAL < EMERGING < MANIFEST < DISSOLVING; `not` keeps polarity and phase |
| `zxv_hex_polarity` | from evidence counts before and after: stale -> ENTROPIC, else the margin \|pos - neg\| growing -> NEGENTROPIC, shrinking -> ENTROPIC, same -> NEUTRAL; never FORBIDDEN |
| `zxv_hex_name`, `zxv_hex_action_name` | static ASCII, e.g. `"true/negentropic/manifest"`, `"rollback"`; `"INVALID"` out of range |

Forbidden is absorbing: `step`, `and`, `or` and `not` of a forbidden tag
stay forbidden. A PROCEED needs base TRUE with a non-entropic polarity.

**Pending decision.** Combined bases follow the existing `zxv_lp_and` and
`zxv_lp_or` tables, so BOTH AND NEITHER is NEITHER and BOTH OR NEITHER is
BOTH. Those two results are still waiting on the owner's decision; if they
change, the `and` and `or` tables in `vectors.json` are regenerated from
libzxv and every SDK picks them up through its conformance test.

### The quarantine log

`zxv_hexlog` is an opaque, mutex-guarded, append-only record of forbidden
work, read for audit and never replayed:

```c
zxv_hexlog *log;
zxv_hexlog_create(cap, 1024, &log);              /* needs ATTEST; 1..65536 */
zxv_hexlog_append(cap, log, tag, reason, submitter, subject32, &seq);
zxv_hexlog_count(log, &n);
zxv_hexlog_head(log, head32);                    /* last hash; zeros if empty */
zxv_hexlog_at(log, i, &seq, &tag, &reason, &who, subj, prev, hash); /* NULL outs allowed */
zxv_hexlog_verify(log);
zxv_hexlog_destroy(log);                         /* before its context */
```

- **Capability: `ATTEST`.** A quarantine record is an attestation about
  refused work of any kind (a model call, a posting, a frame), signed off
  later by whoever holds the head. `LEDGER_POST` means moving capital, and
  `BUDGET` or `PHASE` would let a scheduler write the audit trail of what
  it refused. Create and append re-check the cap; reads take none but fail
  closed on a poisoned context like every other handle. No new bit was
  added.
- **Hash: SHA-256** (the kernel's `robin_debanks/sha256.c`), not SHA3:
  `hash_k = SHA-256("zxv/lpx-qlog/v1" || seq u64 LE || tag || reason ||
  submitter u32 LE || subject[32] || prev[32])`, with `prev_0` all zeros.
  Anyone can recompute the chain from `zxv_hexlog_at`. Signing the head is
  the caller's job.
- `append` refuses a tag that is not forbidden with `ZXV_E_ARG`.
- **A full log is `ZXV_E_CAPACITY` (REFUSED), and the library does not
  halt by itself.** Forbidden work that cannot be recorded must not go on,
  so the caller must halt the context (`zxv_ctx_halt`) or stop the work.
  Pick the capacity for the worst case.
- `verify` recomputes every link; a mismatch is `ZXV_E_INVARIANT` and
  poisons the context.

`vectors.json` gains a `"hex"` section, written by
`tools/gen_vectors.c` from libzxv itself: the 64 tags with names and
actions, `not`, the 64x64 `and`/`or` tables, `step` and `set` (with -1
where the call is refused), polarity cases, the base mapping, and a
three-record quarantine chain with its head.

The header documents every function. The answer key every SDK tests
against is [`sdk/conformance/vectors.json`](../conformance/vectors.json),
regenerated by `make -C sdk/libzxv vectors`.

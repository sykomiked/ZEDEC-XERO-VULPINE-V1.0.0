<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Writing a plugin: Z-Wasm, z-contract, z-audit and Tri-Isolate

**Who this is for:** platform teams that let other people's code run inside
their product (pricing rules, scoring, fee calculators), and the people who
write that code.
**What you will be able to do:** write a small WebAssembly plugin, declare
what it may do in a contract, check it before publishing, and run it with
fuel limits, call checks and host approval, first in-process and then in its
own worker process.

The example is a tip calculator. It takes a bill in kobo and a percentage,
and **proposes** a tip. It cannot pay the tip itself: only the host can do
that, after checking the proposal.

## The pieces

| Kit | Role |
|---|---|
| [Z-Wasm](../../kits/z-wasm/) | loads the plugin, refuses floats and SIMD, meters every instruction as **fuel** paid from a budget, and queues proposals for the host |
| [z-contract](../../kits/z-contract/) | a typed contract: exact bounds on arguments, results and proposals, checked on every call |
| [z-audit](../../kits/z-audit/) | a static check to run in CI before you publish a plugin |
| [Tri-Isolate](../../kits/tri-isolate/) | runs each plugin in a separate worker process |

**WebAssembly** (Wasm) is a portable bytecode format. Many languages can
compile to it, and a host can run it without giving it access to the rest
of the program.

## Before you start

From the repository root:

```sh
make -C sdk/libzxv all
make -C sdk/kits/z-wasm all
make -C sdk/kits/z-contract all
make -C sdk/kits/z-audit all
make -C sdk/kits/tri-isolate all
python3 -m pip install ziglang          # a wasm32 C compiler
SDK=$PWD/sdk
mkdir tip && cd tip
```

## Step 1: write the plugin

Save as `tip.c`:

```c
#include "zxv_plugin.h"
ZXV_EXPORT("tip") int32_t tip(int64_t amount_minor, int64_t percent) {
    zxv_rat t;
    int32_t st = zxv_rat_mul(amount_minor, 1, percent, 100, &t);
    if (st != ZXV_OK) return st;
    static const char label[] = "tip in minor units";
    return zxv_propose(1, t.num, t.den, label, zxv_strlen(label));
}
```

`zxv_rat_mul` and `zxv_propose` are **imports**: functions the host provides.
The plugin gets them only if the host grants them. There is no float
anywhere: the tip is an exact fraction.

Build it:

```sh
python3 -m ziglang cc --target=wasm32-freestanding -O2 -nostdlib -mno-bulk-memory \
  -Wl,--no-entry -Wl,-z,stack-size=16384 -Wl,--initial-memory=131072 -Wl,--max-memory=131072 \
  -I $SDK/kits/z-wasm/examples/plugins tip.c -o tip.wasm
```

`-mno-bulk-memory` and the fixed memory size are needed: Z-Wasm refuses
modules that use bulk-memory instructions or that can grow without a limit.

## Step 2: write the contract

Save as `tip.zc`:

```text
fn tip(amount: int[0..10^9], percent: int[0..30]) -> int[0..0]
    proposes int[0..300000000] max 1;
```

In words: `amount` is a whole number from 0 to a billion, `percent` from 0
to 30, the function returns 0 (success), and it may make at most one
proposal, which must be a **whole number** of kobo from 0 to 300 million.

## Step 3: check it before publishing

```sh
$SDK/kits/z-wasm/build/zw-tool validate --grant rational,propose tip.wasm
# tip.wasm: accepted (1661 bytes)

$SDK/kits/z-contract/build/zc-tool check tip.zc
# fn tip(amount: int[0..1000000000], percent: int[0..30]) -> int[0..0] proposes int[0..300000000] max 1;

$SDK/kits/z-audit/build/z-audit --grant rational,propose --contract tip.zc tip.wasm
```

`z-audit` prints one line per check and ends with:

```text
RESULT: PASS (0 error(s), 0 warning(s))
Bounded static checks. A pass does not prove that no Forbidden state is reachable or that the plugin is correct; the runtime guards (Z-Wasm validation and fuel, z-contract call checks) still apply to every call.
```

It exits with 0 on a pass, 1 on a failure and 2 on a usage error, so a CI
step can gate on it. In this run it also printed a `NOT_ANALYZED` note: the
function body uses an instruction outside the small subset the analyser
follows. That is normal. `z-audit` runs **bounded checks**, not a proof; the
runtime checks in the next step are what protect you.

## Step 4: host it

The host program below:

1. splits its authority into narrow [capabilities](capabilities.md);
2. creates a [budget](budgets-and-time.md) of 1,000,000 fuel units;
3. loads the plugin and grants only `RATIONAL` and `PROPOSE`;
4. attaches the contract, with violations recorded in an evidence log and
   in the [quarantine log](four-valued-logic.md#the-quarantine-log);
5. calls `tip` three times;
6. accepts or rejects each proposal **with its own capability**.

Save as `host.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zcontract_wasm.h"   /* also brings in zwasm.h, zcontract.h and zxv_api.h */

#define TRY(x) do { int32_t st_ = (x); if (st_ != ZXV_OK) { \
    fprintf(stderr, "%s: %s\n", #x, zw_status_name(st_)); exit(1); } } while (0)

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t) n);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) { free(b); b = NULL; }
    fclose(f);
    *len = (size_t) n;
    return b;
}

int main(void) {
    /* 1. The host's authority, split by least privilege. */
    zxv_ctx *ctx;
    zxv_cap *root, *budget_cap, *post_cap, *attest_cap;
    uint32_t till, waiter;
    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    TRY(zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &budget_cap));
    TRY(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &post_cap));
    TRY(zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &attest_cap));
    TRY(zxv_ccy_enable_cap(post_cap, "NGN"));
    TRY(zxv_ledger_open_account_cap(post_cap, "till", "NGN", ZXV_ACCT_ISSUER, &till));
    TRY(zxv_ledger_open_account_cap(post_cap, "waiter", "NGN", ZXV_ACCT_HOLDER, &waiter));

    /* 2. A compute budget that pays for every instruction the plugin runs. */
    zxv_rmag *rm;
    uint32_t fuel;
    TRY(zxv_rmag_create_cap(budget_cap, &rm));
    TRY(zxv_rmag_budget(budget_cap, rm, "plugin fuel", ZXV_RES_COMPUTE_CYCLES, 1000000, 1, 1, &fuel));

    /* 3. Load the plugin: validated, metered, then compiled. */
    zw_host *host;
    zw_plugin *pl;
    size_t len;
    uint8_t *wasm = slurp("tip.wasm", &len);
    TRY(zw_host_create(budget_cap, rm, fuel, &host));
    TRY(zw_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_PROPOSE));
    TRY(zw_plugin_load(host, wasm, len, "tip", &pl));

    /* 4. Attach the contract; violations go to an evidence log and,
     *    through the sink, to the libzxv quarantine log. */
    zc_contract *c;
    zc_evlog *evlog;
    zxv_hexlog *qlog;
    zc_hexsink *sink;
    zc_wasm_guard *guard;
    size_t zlen;
    char *text = (char *) slurp("tip.zc", &zlen), msg[256];
    TRY(zc_parse(text, zlen, &c, NULL, NULL, NULL, msg, sizeof msg, NULL));
    TRY(zc_evlog_create(1024, &evlog));
    TRY(zxv_hexlog_create(attest_cap, 1024, &qlog));
    TRY(zc_hexsink_create(attest_cap, qlog, 0, &sink));
    TRY(zc_hexsink_halt_on_full(sink, ctx));
    TRY(zc_evlog_attach_hexsink(evlog, sink));
    TRY(zc_wasm_attach(pl, "tip", c, "tip", evlog, &guard));

    /* 5. Call it three times. */
    int64_t calls[3][2] = {{2000, 15}, {2000, 50}, {1999, 15}};
    for (int i = 0; i < 3; i++) {
        int32_t st = zw_plugin_call(pl, "tip", calls[i], 2, NULL, NULL, NULL);
        printf("tip(%lld, %lld): %s\n", (long long) calls[i][0], (long long) calls[i][1],
               zw_status_name(st));
        if (st == ZW_E_CHECK) {
            zc_wasm_last_violation(guard, NULL, NULL, NULL, NULL, msg, sizeof msg, NULL);
            printf("  %s\n", msg);
        }
    }

    /* 6. The host decides, with its own capability. */
    uint64_t id, entry;
    while (zw_queue_next(host, &id) == ZXV_OK) {
        int32_t st = zw_proposal_accept_post(host, post_cap, id, till, waiter, "NGN", "TIP-1", &entry);
        printf("proposal %llu: %s\n", (unsigned long long) id, zw_status_name(st));
        if (st != ZXV_OK) TRY(zw_proposal_reject(host, post_cap, id));
    }
    int64_t equity;
    uint32_t quarantined;
    TRY(zxv_ledger_balance_cap(post_cap, waiter, NULL, NULL, &equity));
    TRY(zxv_hexlog_count(qlog, &quarantined));
    TRY(zxv_hexlog_verify(qlog));
    printf("waiter holds %lld kobo; %u quarantine record(s), chain verified\n",
           (long long) equity, quarantined);

    /* 7. Tear down, children first. */
    zc_wasm_detach(guard);
    zw_plugin_destroy(pl);
    zw_host_destroy(host);
    zc_evlog_destroy(evlog);
    zc_hexsink_destroy(sink);
    zxv_hexlog_destroy(qlog);
    zc_contract_destroy(c);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(attest_cap);
    zxv_cap_destroy(post_cap);
    zxv_cap_destroy(budget_cap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    free(wasm);
    free(text);
    return 0;
}
```

Build and run:

```sh
K=$SDK/kits
cc -std=c11 host.c -I $SDK/libzxv/include -I $K/z-wasm/include -I $K/z-contract/include \
   $K/z-contract/build/libzcontract_wasm.a $K/z-contract/build/libzcontract.a $K/z-wasm/build/libzwasm.a \
   -L$SDK/libzxv/build -lzxv -Wl,-rpath,$SDK/libzxv/build -lpthread -lm -o host
./host
```

```text
tip(2000, 15): ZXV_OK
tip(2000, 50): ZW_E_CHECK
  tip: parameter 1 'percent' = 50 is above the maximum 30
tip(1999, 15): ZW_E_CHECK
  tip: proposal 0 = 299.85 (5997/20) is not an integer
proposal 1: ZXV_OK
waiter holds 300 kobo; 2 quarantine record(s), chain verified
```

What happened:

- `tip(2000, 15)` proposed 300 kobo. The host accepted it and posted it.
- `tip(2000, 50)` was stopped **before it ran**: 50 is above the contract's
  maximum of 30.
- `tip(1999, 15)` ran, but its proposal, 299.85 kobo, is not a whole number
  of kobo. The contract stopped it, so the host never saw it.
- Both violations went into the quarantine log, whose hash chain verifies.

If the plugin loops forever, it runs out of fuel and is stopped with
`ZW_E_FUEL`. If it imports a function the host did not grant, it does not
load (`zw-tool validate --grant rational tip.wasm` rejects it with
`IMPORT_DENIED: function import zxv.propose is not on the host's
allow-list`).

## Step 5: run it in its own process

Everything above runs inside the host's process. The contract and fuel stop
a plugin that misbehaves within the rules of WebAssembly. They do not
protect you from a bug in the WebAssembly engine itself. For that, run each
plugin in its own **worker process** with Tri-Isolate. The host keeps the
budget and the proposal queue; the worker only runs the code. The host kills
the worker if it runs out of fuel or time.

The changes from `host.c`, in short (the
[Tri-Isolate README](../../kits/tri-isolate/) has the full API):

```c
ti_host *host;
ti_plugin *pl;
TRY(ti_host_create(budget_cap, rm, fuel, worker_path, &host));
TRY(ti_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_PROPOSE));
TRY(ti_host_set_timeouts(host, 2000, 5000));             /* ms per call, ms to start */
if (ti_seccomp_available()) TRY(ti_host_set_flags(host, TI_F_SECCOMP));
TRY(ti_plugin_load(host, wasm, len, "tip", &pl));

/* Check the contract on the host side, before and after the call. */
TRY(zc_check(c, "tip", ZC_PART_PARAMS, vals, NULL, NULL, NULL, 0, NULL));
TRY(ti_plugin_call(pl, "tip", args, 2, NULL, NULL, NULL));

while (ti_queue_next(host, &id) == ZXV_OK) {
    TRY(ti_proposal_get(host, id, NULL, NULL, &num, &den, name, label, sizeof label, &label_len));
    /* zc_check(..., ZC_PART_PROPOSALS, ...) then accept or reject */
}
```

`worker_path` points at `$SDK/kits/tri-isolate/build/ti-worker`. Pass a
buffer for the label (`uint8_t label[65]`): labels are at most 64 bytes.
Link with `$K/tri-isolate/build/libtriisolate.a`,
`$K/z-contract/build/libzcontract.a` and
`$K/tri-isolate/build/zwasm/libzwasm.a`, and add `-I $K/tri-isolate/include`.
Our run printed `waiter holds 300 kobo (worker seccomp: on)`.

## Honest limits

- **Tri-Isolate is a process boundary, not a virtual machine.** A worker
  shares the kernel with the host. On Linux it can add a **seccomp** filter
  (a kernel list of allowed system calls) and a memory limit (`RLIMIT_AS`).
  macOS has neither. Windows is not supported yet.
- **The in-process host is not a sandbox against engine bugs.** Use
  Tri-Isolate, or a container or VM around it, for code you do not trust.
- **z-audit is bounded checks, not a proof.** A pass means "nothing found
  within these limits".
- **The host is still responsible.** A proposal the contract allows can
  still be wrong for your business. Accept it only with your own checks.

## Next

- [Calling C and inbound data](calling-c-and-inbound-data.md)
- [Troubleshooting](troubleshooting.md#plugins)

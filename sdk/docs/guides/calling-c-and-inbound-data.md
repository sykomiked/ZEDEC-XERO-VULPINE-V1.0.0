<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Calling C libraries and taking in outside data

**Who this is for:** developers who connect ZXV to existing C code (a maths
library, a device driver) or to outside data (HTTP APIs, MQTT sensors, FIX
trading messages).
**What you will be able to do:** generate typed, capability-gated wrappers
for C functions with **z-ffi**, and parse and label inbound records exactly
with **adapter-matrix**.

Both kits are C. Build them once, from the repository root:

```sh
make -C sdk/libzxv all
make -C sdk/kits/z-ffi all
make -C sdk/kits/z-contract all
make -C sdk/kits/adapter-matrix all
SDK=$PWD/sdk; K=$SDK/kits
```

## Part 1: calling C with z-ffi

Most C libraries take and return `double`. ZXV refuses floats at its
boundary (see [Exact numbers](exact-numbers.md)). **z-ffi** bridges the two:
you describe each C function in a small **spec**, and `zf-gen` writes a C
wrapper that checks a capability, checks the ranges, and converts numbers
only when the conversion is exact.

### The spec

Save as `mymath.zf`:

```text
library mymath;
include <math.h>;
include <stdlib.h>;

fn sqrt(x: f64 from rat) -> f64 as rat needs BUDGET;
fn labs(x: long[-LONG_MAX..LONG_MAX]) -> long[0..LONG_MAX] needs BUDGET;
```

- `f64 from rat`: the caller passes an exact fraction. The wrapper turns it
  into a `double` only if the double holds exactly that value.
- `f64 as rat`: the `double` result comes back as its exact fraction.
- `long[-LONG_MAX..LONG_MAX]`: the argument must be in this range. `labs` of
  the most negative `long` is undefined behaviour in C, so the range leaves
  it out and the wrapper never makes that call.
- `needs BUDGET`: the caller must present a capability with the `BUDGET` bit.

### Generate, build, call

```sh
mkdir gen
$K/z-ffi/build/zf-gen mymath.zf gen/mymath_zf.h gen/mymath_zf.c
```

Save as `app.c`:

```c
#include <stdio.h>
#include "mymath_zf.h"

int main(void) {
    zxv_ctx *ctx;
    zxv_cap *root, *math;
    int64_t n, d, r;
    zxv_ctx_create(&ctx);
    zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root);
    zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &math);       /* the bit the spec asked for */

    int32_t st = zf_mymath_sqrt(math, 9, 4, &n, &d);       /* sqrt(9/4) */
    printf("sqrt(9/4)  %s  %lld/%lld\n", zxv_status_name(st), (long long) n, (long long) d);
    st = zf_mymath_sqrt(math, 1, 10, &n, &d);              /* 1/10 is not a double */
    printf("sqrt(1/10) %s\n", zf_status_name(st));
    st = zf_mymath_sqrt(math, -1, 1, &n, &d);              /* NaN */
    printf("sqrt(-1)   %s\n", zf_status_name(st));
    st = zf_mymath_labs(math, -42, &r);
    printf("labs(-42)  %s  %lld\n", zf_status_name(st), (long long) r);
    st = zf_mymath_labs(root, INT64_MIN, &r);              /* undefined in C: never called */
    printf("labs(MIN)  %s\n", zf_status_name(st));
    zxv_cap *reader;
    zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader);
    st = zf_mymath_labs(reader, -42, &r);                  /* wrong capability */
    printf("no BUDGET  %s\n", zf_status_name(st));

    zxv_cap_destroy(reader);
    zxv_cap_destroy(math);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    return 0;
}
```

```sh
cc -std=c11 app.c gen/mymath_zf.c -I gen -I $K/z-ffi/include -I $SDK/libzxv/include \
   $K/z-ffi/build/libzffi.a -L$SDK/libzxv/build -lzxv -Wl,-rpath,$SDK/libzxv/build -lm -o app
./app
```

```text
sqrt(9/4)  ZXV_OK  3/2
sqrt(1/10) ZF_E_INEXACT
sqrt(-1)   ZXV_E_FLOAT
labs(-42)  ZXV_OK  42
labs(MIN)  ZF_E_RANGE
no BUDGET  ZXV_E_CAP
```

Nothing was rounded. When an exact answer was impossible, the call was
refused instead.

**z-ffi is not a sandbox.** The C library you wrap runs with your process's
full privileges. z-ffi checks what goes in and what comes out; it cannot
stop the C code itself from misbehaving. Wrap only libraries you trust, or
put them behind a process boundary.

## Part 2: inbound data with adapter-matrix

Data from outside is untrusted until you have evidence otherwise. **adapter-matrix**
parses it, reads every number exactly, and gives each record a
[hexagram tag](four-valued-logic.md#the-hexagram-tag-state-direction-stage)
that says how far you can trust it.

| Source | Constant |
|---|---|
| a JSON document (an HTTP body) | `ZA_SRC_JSON` |
| an MQTT message (a JSON payload plus its topic) | `ZA_SRC_MQTT` |
| a FIX trading message, `tag=value` separated by SOH (byte 0x01) | `ZA_SRC_FIX` |
| the same with a pipe character as the separator (logs, tests) | `ZA_SRC_FIX_PIPE` |

### Every record gets a label

| Situation | Tag | Action |
|---|---|---|
| parsed and matches the contract, but nobody has vouched for it | `neither/neutral/emerging` | `wait` |
| refused by the contract | `false/neutral/emerging` | `refuse` |
| later attested by the host (needs `ATTEST`) | `true/neutral/manifest` | `proceed` |

An accepted record is still `NEITHER`: "it looks right" is not "it is true".
Only your own evidence, such as a settled ledger entry, moves it to `TRUE`.

### Example

The contract says the amount must be a whole number of kobo (`den|100`: the
denominator divides 100), the currency three bytes, and the reference 1 to
35 bytes. Save as `ingest.c`:

```c
#include <stdio.h>
#include <string.h>
#include "zadapter.h"

static const char CONTRACT[] =
    "fn payment(amount.value: rat[0..10^12] den|100, amount.currency: bytes[3..3],\n"
    "           reference: bytes[1..35]);\n";

static void show(const char *what, za_record *r) {
    int32_t lp, tag, act;
    za_record_state(r, &lp, &tag);
    za_record_action(r, &act);
    printf("%-8s %s -> %s\n", what, zxv_hex_name((uint8_t) tag), zxv_hex_action_name(act));
}

int main(void) {
    const char *good = "{\"amount\":{\"value\":12.50,\"currency\":\"NGN\"},\"reference\":\"E2E-42\"}";
    const char *bad = "{\"amount\":{\"value\":0.105,\"currency\":\"NGN\"},\"reference\":\"E2E-43\"}";
    zc_contract *c;
    zc_evlog *log;
    za_record *r1, *r2;
    uint32_t i;
    int64_t num, den;

    zc_parse(CONTRACT, strlen(CONTRACT), &c, NULL, NULL, NULL, NULL, 0, NULL);
    zc_evlog_create(1024, &log);

    za_ingest(ZA_SRC_JSON, "https://bank.example/payments/42",
              (const uint8_t *) good, strlen(good), &r1);       /* ZXV_OK even if refused */
    za_validate(r1, c, "payment", log);
    show("good", r1);                                           /* neither/neutral/emerging -> wait */
    za_field_find(r1, "amount.value", &i);
    za_field_rat(r1, i, &num, &den);
    printf("amount.value = %lld/%lld\n", (long long) num, (long long) den);   /* 25/2, exactly */

    za_ingest(ZA_SRC_JSON, "https://bank.example/payments/43",
              (const uint8_t *) bad, strlen(bad), &r2);
    printf("validate: %s\n", za_status_name(za_validate(r2, c, "payment", log)));  /* ZA_E_REFUSED */
    show("bad", r2);                                            /* false/neutral/emerging -> refuse */

    /* Later, on the host's own evidence (say the ledger settled it): */
    zxv_ctx *ctx;
    zxv_cap *root, *attest;
    zxv_ctx_create(&ctx);
    zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root);
    zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &attest);
    za_record_attest(r1, attest, ZXV_LP_TRUE);
    show("attested", r1);                                       /* true/neutral/manifest -> proceed */

    za_record_destroy(r1);
    za_record_destroy(r2);
    zc_evlog_destroy(log);
    zc_contract_destroy(c);
    zxv_cap_destroy(attest);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    return 0;
}
```

```sh
cc -std=c11 ingest.c -I $K/adapter-matrix/include -I $K/z-contract/include -I $SDK/libzxv/include \
   $K/adapter-matrix/build/libzadapter.a $K/adapter-matrix/build/zcontract/libzcontract.a \
   -L$SDK/libzxv/build -lzxv -Wl,-rpath,$SDK/libzxv/build -lm -o ingest
./ingest
```

```text
good     neither/neutral/emerging -> wait
amount.value = 25/2
validate: ZA_E_REFUSED
bad      false/neutral/emerging -> refuse
attested true/neutral/manifest -> proceed
```

The JSON number `12.50` became exactly 25/2, never a float. `0.105` is 21/200
NGN, a fraction of a kobo, so the contract refused it. The example skips
status checks to stay short; check every return value in real code.

## Next

- [Writing a plugin](writing-a-plugin.md): the same contracts guard plugin
  calls.
- The kit READMEs: [z-ffi](../../kits/z-ffi/),
  [adapter-matrix](../../kits/adapter-matrix/),
  [z-contract](../../kits/z-contract/).

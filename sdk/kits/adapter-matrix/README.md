<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# adapter-matrix: label inbound records

adapter-matrix turns inbound payloads into records a ZEDEC host can reason
about. It reads three formats:

| Adapter | Input | Notes |
|---|---|---|
| `ZA_SRC_JSON` | a JSON document, e.g. an HTTP response body | strict RFC 8259; nested values become dotted fields (`amount.value`, `items.0.price`) |
| `ZA_SRC_MQTT` | an MQTT PUBLISH payload and its topic name | the payload is JSON, an exact decimal (`21.5`) or UTF-8 text; the topic must be a valid topic **name** (no `+` or `#`) |
| `ZA_SRC_FIX`, `ZA_SRC_FIX_PIPE` | a FIX tag=value message, SOH- or `\|`-delimited | header order, BodyLength and CheckSum are checked; fields get their FIX names (`Price`, `OrderQty`) |

Every number is parsed **exactly** from its decimal text into an `int64`
rational: `12.3450` is 2469/200, `1.5e-3` is 3/2000. Nothing goes through a
float. A number with no exact `int64` form (`1e400`, `1e-30`) refuses the
record; nothing is rounded.

Every record carries a **label**:

| Record | Four-valued state | 64-state tag (libzxv `zxv_hex_*`) | `za_record_action` |
|---|---|---|---|
| accepted: well-formed, and passed its contract if one was checked | `ZXV_LP_NEITHER` | `neither/neutral/emerging` | `WAIT` |
| refused: malformed, or failed its contract | `ZXV_LP_FALSE`, with reasons | `false/neutral/emerging` | `REFUSE` |
| attested by the host (`za_record_attest`, needs `ZXV_CAP_ATTEST`) | `TRUE`, `FALSE` or `BOTH` | `<state>/neutral/manifest` | `PROCEED`, `REFUSE` or `ESCALATE` |

`za_validate` checks a record against a [z-contract](../z-contract/)
function whose parameter names are field names. A failure refuses the
record with the contract's reason and appends a hash-chained evidence
record (origin `ZC_ORIGIN_ADAPTER`) to a `zc_evlog`.

> **An adapter labels data. It cannot make a bad source accurate.** A
> well-formed record that passes its contract is still only "accepted,
> unverified" (`NEITHER`). A sensor that reads 21.5 when the room is at 30
> produces a perfect record. A FIX message that claims to come from
> `BUYSIDE` is labelled `BUYSIDE->EXCHANGE` whether or not BUYSIDE sent
> it. Truth comes only from the host attesting a record, with its own
> capability, on its own evidence. See [Threat model](#threat-model).

Kit API version 0.2 (`include/zadapter.h`). It calls libzxv ABI 1.3 and
z-contract 0.2. New in 0.2: the tag is libzxv's real 64-state tag (it was a
placeholder with the same layout in 0.1), attestation moves it by the
libzxv rules, `za_record_action` says what libzxv's table does with it, and
validation refusals can be quarantined in a libzxv quarantine log (see
[Quarantine log](#quarantine-log-abi-13)).

## Quickstart

```sh
make -C ../../libzxv all     # once
make test                    # adapters, validation, fuzz smoke run, no-float check
make demo
```

`make demo` runs [`examples/ingest_demo.c`](examples/ingest_demo.c): six
payloads (two JSON payments, two FIX orders, two MQTT readings), each
validated against [`examples/market.zc`](examples/market.zc):

```text
https://bank.example/api/payments/42   payment    accepted, unverified [neither/neutral/emerging -> wait]
https://bank.example/api/payments/43   payment    REFUSED [false/neutral/emerging -> refuse]
    CONTRACT: payment: parameter 0 'amount.value' = 0.105 (21/200): denominator 200 does not divide 100
BUYSIDE->EXCHANGE                      new_order  accepted, unverified [neither/neutral/emerging -> wait]
BUYSIDE->EXCHANGE                      new_order  REFUSED [false/neutral/emerging -> refuse]
    CONTRACT: new_order: parameter 3 'Price' = 12.34567 (1234567/100000): denominator 100000 does not divide 10000
plant/7/boiler                         reading    accepted, unverified [neither/neutral/emerging -> wait]
plant/7/boiler                         reading    REFUSED [false/neutral/emerging -> refuse]
    UTF8: MQTT payload is neither JSON, a decimal, nor UTF-8 text
host attests the first payment:        attested [true/neutral/manifest -> proceed]
evidence records: 2, chain verifies
quarantine records: 2, chain verifies
    #0 false/forbidden/emerging, DENOMINATOR
    #1 false/forbidden/emerging, DENOMINATOR
```

The payloads are bytes in the C source: nothing is fetched. The binary MQTT
payload is refused at ingest, so it never reaches the contract and leaves
no contract evidence; the two contract refusals do, and each of them is
also quarantined as a Forbidden record (next section).

`build/za-tool` does the same for one payload file:

```sh
build/za-tool json  payment.json  examples/market.zc payment
build/za-tool fix   order.fix     examples/market.zc new_order
build/za-tool mqtt  reading.txt   examples/market.zc reading plant/7/boiler
```

### In a host

```c
#include "zadapter.h"

za_record *r = NULL;
za_ingest(ZA_SRC_JSON, "https://bank.example/api/payments/42",
          body, body_len, &r);                    /* ZXV_OK even if refused */
int32_t st = za_validate(r, contract, "payment", log);
/* ZXV_OK: accepted, still NEITHER.  ZA_E_REFUSED: FALSE, evidence logged. */

uint32_t i;
int64_t num, den;
if (st == ZXV_OK && za_field_find(r, "amount.value", &i) == ZXV_OK)
    za_field_rat(r, i, &num, &den);               /* 2501/2, exactly */

/* later, once the host has its own evidence (e.g. the ledger settled): */
za_record_attest(r, attest_cap, ZXV_LP_TRUE);     /* needs ZXV_CAP_ATTEST */
za_record_destroy(r);
```

## Adapters

### JSON

Strict RFC 8259: one JSON text, UTF-8 only, no byte order mark, no raw
control characters in strings, `\u` escapes with paired surrogates, no
trailing garbage. The document is flattened into leaves: object keys and
array indices are joined with `.`; a top-level scalar is the field
`value`. Refused: duplicate paths (a key repeated, or a key containing `.`
that collides with a nested path), empty keys, nesting deeper than 32,
more than 1024 leaves, a path longer than 255 bytes. Empty objects and
arrays give no field.

Numbers are JSON numbers (`-0.5`, `1E+2`) parsed exactly. Strings, `true`,
`false` and `null` become TEXT, BOOL and NULL fields.

### MQTT

The adapter takes the topic name and the payload bytes that the broker
client delivered; MQTT framing, QoS and sessions belong to that client.
The topic is checked against the rules for a published topic name (1 to
65535 bytes of UTF-8, no NUL, no wildcard). The payload, after trimming
whitespace, is:

- JSON when it starts with `{` or `[` (the JSON adapter's rules);
- an exact decimal (`21.5`, `-3e2`) as the NUMBER field `value`;
- otherwise UTF-8 text as the TEXT field `value`;
- empty (the MQTT idiom for clearing a retained message): no fields.

A payload that looks like a number but has no exact `int64` form is
refused, never kept as text. Binary payloads are refused (`UTF8`).

### FIX

Tag=value messages, FIX 4.x and FIXT 1.1 framing:

- every field is `TAG=VALUE` and a delimiter; TAG is 1 to 9 digits with no
  leading zero; VALUE is non-empty UTF-8;
- `8` (BeginString), `9` (BodyLength), `35` (MsgType) come first, in that
  order, and `10` (CheckSum) last (`FIX_HEADER`);
- BodyLength counts the bytes from `35=` up to and including the
  delimiter before `10=` (`FIX_LENGTH`);
- CheckSum is three digits, the byte sum before `10=` modulo 256
  (`FIX_CHECKSUM`). With `ZA_SRC_FIX_PIPE` each `|` counts as the SOH it
  stands for, so a logged message keeps its checksum.

Known tags get their FIX names (`Price`, `OrderQty`, `Symbol`, ...);
others are `TagN`. Price, quantity and integer fields must be exact
decimals without exponents. A repeated tag keeps its order with an
occurrence suffix (`MDEntryPx`, `MDEntryPx.1`); repeating groups are not
interpreted. Length-prefixed data fields (RawData and the like) are
refused (`UNSUPPORTED`), because their value may contain the delimiter.

When the caller passes no source label, the record's source is
`SenderCompID->TargetCompID` from the message: what the message **claims**.

The known-answer test is the widely published logon message
`8=FIX.4.2|9=65|35=A|49=SERVER|56=CLIENT|34=177|52=20090107-18:15:16|98=0|108=30|10=062|`.

## Validation

`za_validate(record, contract, fn, log)` looks each parameter of `fn` up
by name among the record's fields:

| Contract type | Accepted fields |
|---|---|
| `int`, `rat` | NUMBER, or TEXT that is an exact decimal (`"12.50"`) |
| `truth` | BOOL, or TEXT `neither` / `true` / `false` / `both` |
| `tag` | TEXT `base/polarity/phase` |
| `bytes` | TEXT (its UTF-8 bytes) |

Fields the contract does not name are allowed. The first failure refuses
the record:

| Reason | Evidence `reason` | When |
|---|---|---|
| `ZA_R_MISSING` | `ARITY` | no field has the parameter's name |
| `ZA_R_TYPE` | `KIND` | the field cannot be that type (`"maybe"` for a truth) |
| `ZA_R_CONTRACT` | the contract's (`BELOW`, `DENOMINATOR`, ...) | the value is outside the contract |

The evidence record cites the contract digest and the record digest
(`za_record_digest`, SHA3-256 over the source kind, label and fields) and
carries the record's tag. A record refused by validation keeps its fields
so the host can inspect it. If the log refuses the evidence (full, or the
sink said no), `za_validate` returns that status and the record is refused
anyway: fail closed.

A record passes `za_validate` once: refused or attested records give
`ZXV_E_STATE`.

## Attestation

`za_record_attest(record, cap, state)` is the only way a record leaves
`NEITHER` other than by refusal. The capability must pass
`zxv_cap_check(cap, ZXV_CAP_ATTEST)`: a live, unrevoked cap holding the
attest bit whose own attestation is `TRUE`. The host may attest `TRUE`,
`FALSE` or `BOTH` (it holds contradicting evidence). A refused record can
only be confirmed `FALSE`. Attestation is once only.

Attestation follows the libzxv tag rules: the base and polarity are
replaced with `zxv_hex_set`, then the phase moves one step, `emerging` to
`manifest`, with `zxv_hex_step`.

## The 64-state tag

Labels are libzxv's 64-state tag (ABI 1.3, `zxv_hex_*` in
[`zxv_api.h`](../../libzxv/include/zxv_api.h)); `za_record_state` gives
the code, and `zxv_hex_name` or `zc_tag_format` its text. Untrusted
inbound data is `neutral` in polarity: an adapter has no basis for calling
a source entropic or negentropic. Its phase is `emerging` until the host
attests it (`manifest`). `za_record_action` returns what libzxv's action
table says (`zxv_hex_action`): `WAIT` for accepted-unverified, `REFUSE`
for refused, `PROCEED` once attested `TRUE`.

## Quarantine log (ABI 1.3)

To keep every validation refusal in a libzxv quarantine log as well,
attach z-contract's ready-made sink to the evidence log you pass to
`za_validate`:

```c
zxv_hexlog *q;  zc_hexsink *hs;  zc_evlog *log;
zxv_hexlog_create(attest_cap, 4096, &q);       /* needs ZXV_CAP_ATTEST */
zc_hexsink_create(attest_cap, q, 0, &hs);
zc_evlog_create(4096, &log);
zc_evlog_attach_hexsink(log, hs);
za_validate(record, contract, "payment", log); /* a refusal lands in both logs */
```

Each refusal becomes one quarantine record: the record's tag made
forbidden (`false/forbidden/emerging`), the z-contract reason, submitter
`ZC_ORIGIN_ADAPTER`, and as subject the hash of the evidence record. The
record itself keeps its own label (`false/neutral/emerging`, `REFUSE`):
the quarantine record is about the refused work, not a new state of the
data. If the quarantine log cannot take the record (full, revoked cap,
halted context), `za_validate` returns `ZC_E_LOG` and the record is
refused anyway. Details are in the
[z-contract README](../z-contract/README.md#the-quarantine-sink-abi-13).

## Threat model

**Assets.** The host's decisions, which must not treat unverified
external data as verified, and an honest record of what was refused and
why.

**Adversaries.** A remote party who controls the payload bytes: a JSON
API, an MQTT publisher, a FIX counterparty, or anyone on the path between
them and the host.

| Protection | Mechanism |
|---|---|
| Unverified data is never labelled true | ingest and validation only produce `NEITHER` or `FALSE`; `TRUE` and `BOTH` need `ZXV_CAP_ATTEST` |
| No float rounding | exact decimal parsing into `int64` rationals (R11: no float type in the kit, checked by `check-nofloat` and compiled with `-mgeneral-regs-only` where the compiler has it) |
| Malformed or ambiguous input is refused, not repaired | strict JSON, duplicate paths, unpaired surrogates, FIX length and checksum, invalid UTF-8, numbers with no exact form |
| Bounded work | 1 MiB payloads, 1024 fields, depth 32, 255-byte names; the JSON reader recurses at most 32 levels |
| Parser robustness | fuzzed under ASan and UBSan (deterministic mutation fuzzer; libFuzzer entry point) |
| Refusals are recorded | hash-chained evidence (z-contract's log) citing the contract and the record digests |

**Out of scope, and the honest limits:**

- **Accuracy.** An adapter checks form; a contract checks bounds. Neither
  can tell whether the source told the truth. A record that passes both is
  `NEITHER`.
- **Authenticity.** The kit does not authenticate anybody. TLS, MQTT
  client authentication and FIX session logon belong to the transport. A
  FIX source label is what the message claims; an MQTT topic is whatever
  the publisher chose.
- **FIX semantics.** Header order, length, checksum and numeric fields are
  checked. Sessions, sequence numbers, repeating-group structure,
  message-type rules and the data dictionary beyond the numeric fields are
  not.
- **JSON numbers that are not exact `int64` rationals** are refused, so
  very large integers (IDs above 2^63) must be sent as strings.
- **Field-name collisions are refused, not resolved.** `{"a.b":1}` and
  `{"a":{"b":1}}` both give the field `a.b`; a document with both is
  refused as a duplicate.
- **In-process.** The kit runs in the host's process and trusts the host.
- **Threads.** A record is not thread-safe; distinct records are
  independent.
- **Kit API 0.x.** It is not yet covered by the libzxv stability promise.

## Limits

| Limit | Value |
|---|---|
| payload | 1 MiB (`ZA_MAX_PAYLOAD`) |
| fields per record | 1024 |
| JSON nesting | 32 |
| field name (path) | 255 bytes |
| reasons kept per record | 16 |
| FIX tag | 9 digits |
| numbers | `int64` numerator and denominator after reduction |

## Tests

```sh
make test                          # everything below except the long runs
make SANITIZE=1 OUT=build/san test # the same under ASan + UBSan (z-contract rebuilt too)
make fuzz FUZZ_ITERS=500000        # deterministic mutation fuzzer
ZA_FUZZ_SEED=3 make fuzz           # another deterministic stream
make fuzz-libfuzzer                # libFuzzer, if clang ships its runtime
```

| Suite | What |
|---|---|
| `test_adapters` | 394 checks: a table of exact decimals (reduction, exponents, the `int64` edges, refusals) cross-checked with `zxv_rat_parse`; JSON accept and refuse cases (nesting, arrays, escapes and surrogates, duplicates, every limit at and one past its value); MQTT topics and payload kinds; FIX known-answer logon, a new order with SOH and `\|`, repeating groups, unknown tags, and refusals for checksum, body length, header order, leading-zero tags, exponents in prices, data fields and invalid UTF-8 |
| `test_validate` | 310 checks against `examples/market.zc`: passing JSON, FIX and MQTT records stay `NEITHER`; every refusal kind (`MISSING`, `TYPE`, `CONTRACT` with `BELOW`, `ABOVE`, `DENOMINATOR`, `LENGTH`, `NOT_INTEGER`, `NOT_IN_SET`) with its evidence record (origin, reason, contract and record digests, text, chain); a full log and a refusing sink (fail closed); attestation with and without `ZXV_CAP_ATTEST`, with a contradictory and a revoked cap, once only, and of refused records; kit API 0.2: every label is the libzxv tag (`zxv_hex_unpack`, `zxv_hex_name`), `za_record_action` for accepted, refused and attested records, and the quarantine sink (a refusal quarantined as `false/forbidden/emerging` pointing at its evidence record, a passing record not quarantined, a full quarantine log failing closed) |
| `fuzz_adapters` | for every input: deterministic records and digests; consistent labels and reasons; unique field names; each NUMBER reduced, re-parsed from its own text, agreeing with `zxv_rat_parse` and surviving `zxv_rat_format`; validation leaves `NEITHER` or gives `FALSE` with one verified evidence record. FIX mutants get their BodyLength and CheckSum recomputed most of the time, so they reach the field parser |
| `ingest_demo` | the demo runs as part of `make test` |
| `check-nofloat` | R11 over the kit sources, tests and example (z-wasm's checker) |

Clean runs on this branch: the suites under gcc and clang, the same under
ASan + UBSan with leak detection, and 4 x 250,000 sanitized fuzz mutants
(seeds 1-4). The sanitizer runs found one bug, now fixed and tested: an
empty JSON key passed a NULL pointer to `memchr`; empty keys are now
refused.

## Files

| Path | |
|---|---|
| `include/zadapter.h` | the kit API |
| `src/za_json.c`, `src/za_mqtt.c`, `src/za_fix.c` | the adapters |
| `src/za_decimal.c` | exact decimal parsing |
| `src/za_record.c`, `src/za_validate.c` | records, labels, digests, validation, attestation (R11: integer code only) |
| `examples/market.zc` | the contract the examples and tests use |
| `examples/ingest_demo.c` | the demo above |
| `tools/za_tool.c` | `za-tool`: one payload file through an adapter |

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.

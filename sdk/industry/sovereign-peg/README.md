<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Sovereign-Peg: an operator toolkit over the V-Bill hub

Sovereign-Peg is a C11 library for the engineers who run a libzxv V-Bill
hub (`zxv_vhub`, ABI 1.5). It covers these tasks:

- registering test currencies;
- applying issuer-signed pegs;
- issuing only what is backed;
- judging cross-currency flows with 64-state capital controls;
- keeping a signed operator decision log.

The API is [`include/zspeg.h`](include/zspeg.h).

**This kit is operator-only. It is not app-facing.** Every call that
changes anything takes an operator capability (`ZSP_CAP_OPERATOR`: ledger
post, ledger read, sign, attest) from the context the desk was created
on. A cap that lacks a right, is revoked, is not attested TRUE, or comes
from another context is refused with `ZSP_E_OPERATOR`, and nothing is
logged. Nothing here should be reachable from
a wallet, a shop, a game or any end-user app.

The kit re-derives none of the hub's economics. Quotes, swaps, pegs,
limits and epochs are the library's own `zxv_vhub_*` calls. The kit adds
three things on top: a backed-only issuance rule, the capital controls,
and the decision log.

## What an operator does

| Step | Call | Library underneath |
|---|---|---|
| register a test currency, fund its pool and V-Bill float | `zsp_ccy_add` | `zxv_vhub_add_ccy`, `zxv_vhub_issue` |
| write a peg exactly | `zsp_peg_parse("1.50", 2, &num, &den)` | none (pure, never rounded) |
| apply the issuer's signed peg | `zsp_peg_set` | `zxv_vhub_set_peg` |
| quote | `zsp_quote` | `zxv_vhub_quote` |
| issue (backed only) | `zsp_issue` | `zxv_vhub_issue` |
| per-sender limits per epoch | `zsp_limit_add`, `zsp_epoch_next` | `zxv_vhub_add_limit`, `zxv_vhub_next_epoch` |
| submit a flow | `zsp_flow_submit` | capital controls, then `zxv_vhub_swap` |
| release or deny a held flow | `zsp_decide` with an officer's signature | `zxv_vhub_swap` on release |
| audit | `zsp_check`, `zsp_coverage`, `zsp_log` | `zxv_vhub_check` |

## Backed-only issuance

The library already keeps every pool and V-Bill float from going below
zero. On top of that, `zsp_issue` to a holder account and `zsp_peg_set`
are refused with `ZSP_E_UNBACKED` if they would break this rule:

```
circulation * den <= float * num        (compared exactly with zxv_rat_cmp)
circulation = issuer CREDIT - hub pool equity   (read from zxv_vhub_balance)
```

`num/den` is the peg in minor units per V-Bill. A swap moves circulation
and float by the same peg value, so swaps keep the rule. Issuing to a pool
(hub liquidity) or V-Bills to a float is always allowed.

## Capital controls

`zsp_flow_submit` tags each flow with a hexagram (`zxv_hex_*`):

- **base**: `zxv_lp_from_evidence(approvals, objections)`. Approvals only
  give TRUE, objections only give FALSE, both give BOTH, and neither gives
  NEITHER.
- **polarity**: FORBIDDEN if the sender is blocked, or if the flow is an
  outflow above the currency's per-flow cap. NEGENTROPIC when two or more
  approvals agree and nobody objects. NEUTRAL otherwise.
- **phase**: POTENTIAL at submission, MANIFEST once swapped.

A 64-entry policy table maps each tag to ALLOW, HOLD or REFUSE. Its
default comes from `zxv_hex_action`. The operator may change entries, with
three locks:

- a FORBIDDEN tag always refuses and is quarantined (`ZSP_E_QUARANTINED`);
- a BOTH tag can never be set to ALLOW (`ZSP_E_POLICY`);
- a FALSE tag can never be set to ALLOW (`ZSP_E_POLICY`).

A held BOTH flow is written to the quarantine log and waits for a person.
A registered officer signs `zsp_decision_message` under the context
`"sovereign-peg/decision"`:

```
"zsp/decision/v1" || u32le flow id || flow record hash[32] || u8 decision
```

A decision is applied once. The kit refuses these:

- a replayed decision (`ZIND_E_REPLAY`);
- a forged decision (`ZXV_E_VERIFY`, quarantined);
- a decision from a key that is not an officer (`ZSP_E_OFFICER`).

A flow that policy allows can still be refused by the library, for
example over a limit or for lack of funds. That flow ends FAILED, with the
library's status.

## Example

```c
zsp_desk_create(op, hub_seed, desk_key, qlog, &d);
zsp_ccy_add(d, op, 0, "TESTA", 2, issuer_pk, 1000000, 1000);
zsp_peg_parse("1.50", 2, &num, &den);                     /* 150/1 */
zsp_peg_message(0, num, den, 1, msg);                     /* the issuer signs msg */
zsp_peg_set(d, op, 0, num, den, 1, issuer_sig);
zsp_issue(d, op, holder, 150000);                         /* ZXV_OK */
zsp_issue(d, op, holder, 1);                              /* ZSP_E_UNBACKED */
```

`make demo` runs [`examples/peg_demo.c`](examples/peg_demo.c). It shows
two pegged test currencies, an exact quote, an issue refused as unbacked,
a flow quarantined over the outflow cap, and a conflicting flow held and
then released by an officer (the replayed decision is refused).

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/sovereign-peg test     # 1,329 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/sovereign-peg SANITIZE=1 OUT=build/san test
make -C sdk/industry/sovereign-peg fuzz FUZZ_ITERS=20000
```

`test_peg` covers these areas:

- operator caps that lack a right, are revoked, are unattested, or come
  from a foreign context;
- exact peg parsing: decimals, fractions, overflow, garbage;
- signed pegs that are forged, stale or unbacked;
- the backed-only rule at its exact boundary;
- quotes against `zxv_vhub_quote`;
- every policy lock;
- flows that are allowed, held, refused, quarantined or failed;
- officer decisions that are forged, replayed or made by a non-officer;
- limits across epochs;
- the signed log.

`fuzz_peg` runs random operator sessions against a model. After each
operation it checks these invariants:

- `zxv_vhub_check` passes and every pegged currency stays covered;
- the holders of a currency sum to its circulation;
- a flow moves money only when it is swapped, and then by the quoted amounts;
- a decision applies only to a held flow, once.

Its output, including the log head and a digest of all balances, is the
same under gcc and clang for a given `ZIND_FUZZ_SEED`.

## Limits

- **Test currencies only, no real value.** The hub is the library's
  in-process demonstration hub. It refuses ISO 4217 codes, and so does
  this kit. No fiat enters or leaves through this kit: there is no on-ramp
  and no off-ramp.
- **Not connected to any central bank,** settlement rail or issuer.
  Nothing here is a legal, regulatory or compliance check. The capital
  controls are a policy engine the operator configures, not advice.
- **V-Bills are units of the hub's accounting,** not an investment. This
  kit offers no yield, interest or return on them.
- The approvals and objections on a flow are counts the operator supplies.
  The kit does not verify who gave them. Only officer decisions on held
  flows are signature-checked.
- The library names fee bucket 1 `ZXV_FEE_VBILL_DIVIDEND`. This kit does
  not charge fees. The name is the library's, not a promise of payments.
- The desk is in memory (up to 65,536 flows, 16 officers, 64 blocked
  senders) and is not thread-safe. Persist `zsp_log` for durability.

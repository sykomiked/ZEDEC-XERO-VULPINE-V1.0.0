<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Logix-Clear: container release across jurisdictions

Logix-Clear is a C11 library that tracks containers through customs on a
four-valued decision. Every step is signed, and duties are exact. It is
**not connected to any customs authority**. It does not file
declarations, it knows no real tariff schedule, and it makes no customs or
legal determination. It recommends, and licensed brokers and the
authorities decide. The API is [`include/zlogix.h`](include/zlogix.h).

## The lifecycle is the hexagram phase

| State | Phase | Call |
|---|---|---|
| booked | Potential | `zlc_book` (ISO 6346 container number, route of 1..8 jurisdictions), `zlc_line_add` (HS code, origin, customs value) |
| in transit | Emerging | `zlc_depart`: lines are locked |
| released | Manifest | `zlc_release`, or a broker's signed decision |
| closed | Dissolving | `zlc_close` |

The phase moves one Gray step at a time (`zxv_hex_step`), so the tag of
every shipment always shows where it is. A held shipment keeps its
Emerging phase with base BOTH. A refused one has base FALSE.

## How release is decided

Each jurisdiction has a rule table. A rule has an HS-code prefix, an
origin (or `*`), an action and an ad-valorem duty rate. The actions are
RELEASE (TRUE), REFUSE (FALSE) and INSPECT (NEITHER). For each declared
line, the best rule is the longest prefix, then an exact origin over `*`.
Within one jurisdiction the lines are combined with four-valued AND. That
result is then joined with the rulings the operator recorded for that
jurisdiction (`zlc_ruling_record`) by `zxv_lp_from_evidence`.

Across the route:

| Jurisdictions say | Decision | `zlc_release` does |
|---|---|---|
| all TRUE | TRUE | posts every duty, appends a signed release record, Manifest; `ZXV_OK` |
| any FALSE, none TRUE | FALSE | appends a signed refusal; `ZLC_E_REFUSED` |
| some TRUE and some FALSE, or any BOTH | **BOTH** | holds and freezes the shipment, writes the quarantine log; `ZIND_E_HELD` |
| otherwise (someone has not ruled) | NEITHER | nothing changes; `ZLC_E_PENDING` |

A held shipment is **never released automatically**. Calling
`zlc_release` again returns `ZIND_E_HELD`. Only a registered broker can
move it, by signing `zlc_broker_message`. That message names the
shipment, its container, the hash of its hold record and the decision
(release or refuse), under the context `"logix-clear/broker"`. The broker's
decision is applied with `zlc_broker_resolve`, and the desk's capability
must include `ATTEST`. A key that is not a registered broker is refused
(`ZLC_E_BROKER`). A signature that does not verify is refused and logged to
the quarantine log as a Forbidden event.

## Duties are exact

`duty = customs value x rate` is an exact rational per line and per
jurisdiction. It is converted to minor units under an explicit rounding
rule:

- The default is `ZIND_ROUND_EXACT`. Under it, 5% of 1234.50 = 61.725 is
  **refused** (`ZIND_E_INEXACT`) instead of being rounded silently.
- With `zlc_set_rounding(d, ZIND_ROUND_HALF_EVEN)`, it posts 6172 cents
  and records the exact residual of 0.005 (`zlc_duty`).

Duties are posted from the payer account to a `duty:<CODE>` ledger account
per jurisdiction. They go through libzxv's `*_cap` ledger calls, under a
reference prefixed with the desk's key id. If the payer is short of funds
(`ZXV_E_FUNDS`), the shipment is left unchanged.

## Example

```c
zlc_desk *d;
zlc_desk_create(cap, "USD", desk_key, qlog, &d);
zlc_jurisdiction_add(d, "US", &us_duty);
zlc_jurisdiction_add(d, "CN", &cn_duty);
zlc_rule_add(d, &(zlc_rule){"US", "8471", "*", ZLC_RULE_RELEASE, {1, 20}});
zlc_rule_add(d, &(zlc_rule){"CN", "8471", "*", ZLC_RULE_RELEASE, {0, 1}});
zlc_broker_add(d, zind_signer_pk(broker));

const char *route[] = {"CN", "US"};
zlc_book(d, "CSQU3054383", route, 2, payer, &s);
zlc_line_add(d, s, "847130", "CN", (zind_qty){2469, 2});   /* 1234.50 */
zlc_depart(d, s);
zlc_set_rounding(d, ZIND_ROUND_HALF_EVEN);
zlc_release(d, s, &info);  /* ZXV_OK: 6172 cents to duty:US */
```

`make demo` runs [`examples/customs_demo.c`](examples/customs_demo.c).
It shows an exact-rule refusal and a half-even release. It then shows a
second container with conflicting rulings, which is held as BOTH and
refused by a broker's signed decision. At the end, the record chain is
verified.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/logix-clear test     # 1,401 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/logix-clear SANITIZE=1 OUT=build/san test
make -C sdk/industry/logix-clear fuzz FUZZ_ITERS=20000
```

`test_logix` covers these areas:

- ISO 6346 check digits;
- capability and argument refusals;
- the rule-precedence table;
- every decision row above, and the hold freeze;
- broker decisions: forged, by an unregistered key, for another shipment,
  or replayed;
- exact and rounded duties at every rounding mode;
- an overflowing value refused;
- insufficient funds leaving everything unchanged;
- ledger balance checks;
- chain tamper and truncation.

`fuzz_logix` runs random operations against a small rule table. After
each one it checks several things:

- the phase matches the state;
- a held shipment is BOTH, and only a genuine broker decision moves it;
- a release succeeds only when `zlc_evaluate` said TRUE, and is held
  exactly when it said BOTH;
- money is conserved across the payer and the duty accounts, and
  `zxv_ledger_check` passes.

Every 100 operations, the record chain is verified.

## Limits

- **Not a customs system.** Rules, rates and rulings are whatever the
  operator enters. Nothing is sent to or received from any authority, and
  no real tariff data ships with the kit.
- Only ad-valorem duties are supported: value x rate, with a rate of at
  most 10. Specific duties, quotas, anti-dumping measures, VAT on
  duty-inclusive value, preferences and currency conversion are not
  modelled.
- A desk works in one currency. Values are taken as declared, and the kit
  cannot check a declaration against the goods.
- A broker's signature shows that a registered key decided. It does not
  show that the person was licensed or right.
- The desk is in memory (up to 65,536 shipments) and is not thread-safe.
  Persist `zlc_records` (a `zind_chain`) if you need durability.

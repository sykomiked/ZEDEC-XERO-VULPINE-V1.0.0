<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tri-Trace: signed chain of custody for serialized items

Tri-Trace is a C11 library that gives every serialized item, such as a
medicine pack, a luxury bag or a tagged part, its own hash-chained record
of custody. Each event is signed with the ML-DSA-87 key of the party that
scanned it. A scan that does not fit the history freezes the item as BOTH
for a human reviewer. A whole chain can be checked offline by anyone who
holds the public keys. The API is [`include/zttrace.h`](include/zttrace.h).

## Custody events

| Event | Who may scan it | State after | Phase |
|---|---|---|---|
| `ZTT_EV_COMMISSION` | any registered party (the maker): the tag starts its chain | held by that party | Potential |
| `ZTT_EV_SHIP` | the current holder, naming a registered destination | in transit | Emerging |
| `ZTT_EV_RECEIVE` | only the named destination | held by it | Emerging |
| `ZTT_EV_DISPENSE` | the current holder: to the end user | dispensed (terminal) | Manifest |

`ztt_scan(r, item, party, scanner_key, kind, to, time)` checks that the
key is the registered key of `party` (`ZTT_E_PARTY` otherwise). It then
classifies the scan:

- **Fits the history:** recorded, signed by the scanner, `ZXV_OK`.
- **Exact same scan again** (same tag, kind, party, destination and time):
  this is a replay. It is refused with `ZIND_E_REPLAY` and **not** recorded.
- **Does not fit:** this is a duplicate. One tag is seen in two places, or
  in conflicting order. The scan is **recorded as evidence**, the item is
  frozen as **BOTH**, the quarantine log gets an entry, and the call
  returns `ZTT_E_CONFLICT`. Duplicates include:
  - the tag commissioned a second time (a clone);
  - a receipt by a party it was not shipped to;
  - a shipment by someone who does not hold it;
  - any scan after it was dispensed;
  - a time earlier than its last event.

A frozen item refuses every further scan (`ZIND_E_HELD`). Nothing
automatic resolves it. A registered reviewer, who is a person, signs
`ztt_review_message` under the context `"tri-trace/review"`. The message
binds the tag, the hash of the first conflicting record, and one of two
outcomes:

- **GENUINE:** custody resumes with a named registered party (TRUE).
- **COUNTERFEIT:** the tag becomes Forbidden for good. Every later scan is
  `ZIND_E_FROZEN`, and the quarantine log records it.

`ztt_review_resolve` applies the decision, and the registry's capability
needs `ATTEST`. The review record carries the reviewer's full signature
and is appended by the registry operator. A forged review is refused and
logged.

## Offline verification

`ztt_item_export` writes an item's chain as self-contained bytes.
`ztt_verify_export(cap, item, bytes, len, trust, &verdict)` needs only a
trust set: the registered party ids and keys, the reviewer keys and the
operator key. It checks these things:

1. every hash link and every ML-DSA-87 signature;
2. each scan is signed by the party it names (`ZIND_E_UNTRUSTED` if not),
   and each destination is a known party;
3. each review is signed by the operator, carries a trusted reviewer's
   valid signature, and refers to the conflict actually recorded;
4. replaying the custody rules from scratch gives the verdict.

The verdict's `lp` is one of three values:

- **TRUE:** consistent custody;
- **BOTH:** an unresolved conflict;
- **FALSE:** declared counterfeit.

The verdict also gives the state, the holder, the number of events and the
number of conflicts. A chain presented under another tag is refused
(`ZTT_E_ITEM`). Truncated or reordered bytes, and any flipped bit, fail
verification.

## Example

```c
ztt_registry_create(cap, operator_key, qlog, &r);
ztt_party_add(r, "MAKER", maker_pk);   ztt_party_add(r, "PHARMACY", pharmacy_pk);
ztt_reviewer_add(r, reviewer_pk);

ztt_scan(r, "01.09506000134352.21.SN1001", "MAKER", maker, ZTT_EV_COMMISSION, NULL, t);
ztt_scan(r, "01.09506000134352.21.SN1001", "MAKER", maker, ZTT_EV_SHIP, "PHARMACY", t + 100);
ztt_scan(r, "01.09506000134352.21.SN1001", "PHARMACY", pharmacy, ZTT_EV_RECEIVE, NULL, t + 200);

ztt_item_export(r, "01.09506000134352.21.SN1001", buf, sizeof buf, &len);
ztt_verify_export(cap, "01.09506000134352.21.SN1001", buf, len, &trust, &v);  /* TRUE, held at PHARMACY */
```

`make demo` runs [`examples/trace_demo.c`](examples/trace_demo.c). It
follows one pack from the maker through a wholesaler to a pharmacy,
refusing a replayed receipt and an impersonated scan along the way. It
then follows a cloned serial seen at two pharmacies, which is frozen as
BOTH and declared counterfeit by the reviewer. Both chains are checked
offline, once as exported and once with a flipped bit.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/tri-trace test     # 627 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/tri-trace SANITIZE=1 OUT=build/san test
make -C sdk/industry/tri-trace fuzz FUZZ_ITERS=20000
```

`test_trace` covers these areas:

- setup and scan refusals, and party capacity;
- a full custody path;
- replays;
- every kind of duplicate listed above;
- reviews that are forged, for another item, for the wrong outcome, by an
  unregistered key, or made without `ATTEST`, plus genuine and counterfeit
  reviews;
- offline verification:
  - a bit flipped at about 60 offsets spread across the export;
  - truncation;
  - the wrong tag;
  - a missing party, reviewer or operator key;
  - a party signing in another party's name.

`fuzz_trace` runs random scans against an independent custody model:

- fitting and non-fitting scans, at times that sometimes run backwards;
- impersonations and exact replays;
- reviews: genuine, forged, by an unregistered key, or for the wrong
  outcome.

It checks every status, state and holder against the model. Every 100
operations it verifies a random chain offline and compares the verdict
with the model. Every 400 operations it flips a bit in an export and
truncates it, and requires both to fail.

## Limits

- **A scan proves that a key signed. It does not prove which object was in
  front of the scanner.** A copied label scanned in the right order looks
  genuine until the original turns up somewhere else. Tri-Trace detects
  the *conflict*, and people decide which one is real. Physical
  authentication (tamper-evident seals, chemical or optical markers) is
  outside the kit.
- Times are the scanning devices' clocks. The kit only requires them to
  be non-decreasing per item. A time that runs backwards is treated as a
  conflict, not corrected.
- A party's key may be stolen. Revoking and rotating keys is up to the
  operator, who removes it from the trust set given to verifiers.
- Tag formats such as GS1 SGTIN are accepted as opaque identifiers of up
  to 64 characters in `[A-Za-z0-9._:/-]`. The kit does not parse or check
  them. No claim of DSCSA, EU FMD or any other serialization compliance is
  made, and integrations with traceability platforms are public-API
  integrations, not partnerships.
- Each scan record carries a 4,627-byte ML-DSA-87 signature, so a 6-event
  chain exports to about 28 KB. The registry is in memory (up to 65,536
  items and 64 parties) and is not thread-safe.

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tri-Deliver: courier escrow released by an offline handoff proof

Tri-Deliver is a C11 library that holds a delivery payment in escrow on
the libzxv ledger. The money is released only when the recipient signs
for the parcel on their own device. That device can be offline, and the
signature can be Ed25519 or ML-DSA-87. A dispute freezes the money as
BOTH until a human arbiter signs a decision. The API is
[`include/ztdeliver.h`](include/ztdeliver.h).

## Lifecycle

| State | Phase | Call | Money |
|---|---|---|---|
| escrow | Potential | `ztd_parcel_create` | sender -> escrow |
| out for delivery | Emerging | `ztd_dispatch(parcel, courier, time)` | in escrow |
| delivered | Manifest | `ztd_handoff_submit` with a valid recipient signature | escrow -> payee |
| disputed | BOTH, frozen | `ztd_dispute` | stays in escrow |
| refunded / cancelled | - | an arbiter's `REFUND_SENDER`, or `ztd_cancel` before dispatch | escrow -> sender |
| closed | Dissolving | `ztd_close` | - |

## The handoff proof

The recipient's device signs the bytes from `ztd_handoff_message`. The
function is pure, so a recipient app can build the same bytes itself:

```
"ztd/handoff/v1" || u8 len || parcel id || u8 len || courier id || u64le unix time
```

An Ed25519 key signs these bytes as they are. An ML-DSA-87 key signs them
under the context `"tri-deliver/handoff"`. The desk then checks four things:

- the signature verifies under the recipient key registered at parcel creation;
- the courier is the courier the parcel was dispatched with (`ZTD_E_COURIER`);
- the time is within the delivery window after dispatch, from 1 s to 30 days (`ZTD_E_WINDOW`);
- the proof has never been accepted before.

A proof is accepted **once**. The desk refuses these:

- the same proof again (`ZIND_E_REPLAY`);
- a second handoff for a delivered parcel;
- a proof made for another parcel;
- any proof on a disputed parcel (`ZIND_E_HELD`).

A signature that does not verify is refused (`ZXV_E_VERIFY`) and goes to
the quarantine log.

## Disputes

`ztd_dispute(parcel, reason)` freezes a parcel that is in escrow or out
for delivery as BOTH. Its money stays in escrow. Nothing automatic moves it
afterwards. A registered arbiter, who is a person, signs
`ztd_arbiter_message` under the context `"tri-deliver/arbiter"`. That
message binds the parcel id, the hash of its dispute record and the
outcome: `ZTD_PAY_PAYEE` or `ZTD_REFUND_SENDER`. The decision is applied
with `ztd_arbiter_resolve`, and the desk's capability needs `ATTEST`. A
forged decision is refused and logged.

## Example

```c
ztd_desk_create(cap, "USD", desk_key, qlog, &d);
ztd_parcel_create(d, "PARCEL-0001", shop, courier_co, 1250, ZTD_ALG_ED25519,
                  recipient_pk, 32, 3600, &p);
ztd_dispatch(d, p, "rider-17", t0);
/* on the recipient's phone, offline: */
ztd_handoff_message("PARCEL-0001", "rider-17", t0 + 900, msg, sizeof msg, &len);
zxv_ed25519_sign(phone_cap, recipient_sk, msg, len, sig);
/* back at the desk, whenever it syncs: */
ztd_handoff_submit(d, p, "rider-17", t0 + 900, sig, 64);  /* ZXV_OK: 1250 to the payee */
ztd_handoff_submit(d, p, "rider-17", t0 + 900, sig, 64);  /* ZIND_E_REPLAY */
```

`make demo` runs [`examples/courier_demo.c`](examples/courier_demo.c):
one delivery with a replay refused, then a disputed parcel refunded by an
arbiter.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/tri-deliver test     # 729 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/tri-deliver SANITIZE=1 OUT=build/san test
make -C sdk/industry/tri-deliver fuzz FUZZ_ITERS=20000
```

`test_deliver` covers these areas:

- creation refusals and both signature algorithms;
- handoff proofs that are replayed, doubled, for another parcel, for the
  wrong courier, outside the window, or forged;
- disputes, and arbiter decisions that are forged, replayed, or made
  without `ATTEST`;
- cancellation;
- conservation of money on the ledger;
- the signed record chain.

`fuzz_deliver` runs random parcels and hostile proofs (replays across
parcels, wrong couriers, out-of-window times, wrong keys and flipped
bits) against a model of each parcel. After each operation it checks
these invariants:

- escrow on the ledger equals what the desk owes;
- a payee is paid at most once;
- a disputed parcel moves only through a genuine arbiter decision.

## Limits

- **Time comes from devices.** The handoff time is what the recipient's
  device signed, and the dispatch time is what the caller passes. The kit
  checks the window between them but cannot tell whether either clock was
  right. Use a trusted time source if that matters.
- **A signature proves a key, not a person or a parcel.** If the
  recipient's key is stolen, or the recipient signs without receiving the
  goods, the proof is still valid. Disputes exist for those cases. They
  are decided by people.
- Not a payment processor or money transmitter. Money moves only between
  accounts on the libzxv ledger, and getting real money in and out is
  outside this kit. Integrations with courier platforms are public-API
  integrations, not partnerships, and no platform has endorsed this kit.
- One currency per desk, in minor units (`int64`). Fees, partial deliveries
  and split payouts are not modelled.
- The desk is in memory (up to 65,536 parcels) and is not thread-safe.
  Persist `ztd_records` for durability.

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tri-Modem: a neighbourhood mesh fallback planner

Tri-Modem is a C11 library for home routers. When a home's own WAN link is
down, it chooses neighbour routers to relay through and shares their
bandwidth fairly. It works only from what the neighbours sign and what
the home's own probes show. The output is a **plan**: which flow goes
through which neighbour, and at how many kbit/s, exactly. Tri-Modem
configures no router, radio or route. Applying a plan, or ignoring it, is
up to the integrator. The API is [`include/ztmodem.h`](include/ztmodem.h).

## Signed adverts

Each neighbour router publishes an advert:

- its sequence number, which must strictly increase;
- when it was issued and when it expires (at most 24 hours later);
- whether its own WAN is up;
- the medium (Ethernet, Wi-Fi, powerline, fibre, amateur radio);
- the bandwidth it offers, as an exact rational in kbit/s, and its latency;
- up to eight destinations it claims it can or cannot reach.

The advert is signed with **Ed25519** (`ztm_advert_seal_ed25519`, 64-byte
signature) or **ML-DSA-87** (`ztm_advert_seal_mldsa`, under the context
`"tri-modem/advert"`). `ztm_advert_accept(p, bytes, len, now)` verifies it
against the key registered for that neighbour:

| Advert | Result |
|---|---|
| valid, newer sequence | taken, `ZXV_OK` |
| a destination claimed both reachable and unreachable | taken, that destination held as **BOTH**, logged: `ZIND_E_HELD` |
| same or older sequence (a replay) | refused, logged: `ZIND_E_REPLAY` |
| expired, or dated more than 120 s ahead | refused: `ZTM_E_EXPIRED` |
| bad signature, wrong algorithm for the key | refused, logged: `ZXV_E_VERIFY` |
| medium is amateur radio | refused: `ZTM_E_MEDIUM` |
| unknown neighbour | `ZTM_E_NEIGHBOUR` |

## Evidence held as BOTH

For each neighbour and destination, the advert's claim and the home's own
probes (`ztm_probe_record`) are joined four-valued. A probe counts as
evidence for 300 s. When a fresh probe contradicts the advert (the
neighbour claims the internet, but traffic through it does not get
there), the pair is **BOTH**. It is held and logged, and that neighbour is
not used for that destination. Nothing picks a side automatically.

The household's **owner** resolves a hold. The owner signs
`ztm_owner_message` with their Ed25519 key, and the message binds:

- this router, the neighbour and the destination;
- the advert's sequence number;
- the probe's time and value;
- the decision: **USE** (reachable through that neighbour) or **AVOID**.

`ztm_hold_resolve` applies the decision until the neighbour's next advert.
A forged decision is refused and logged (`ZTM_E_OWNER`), and so is one
signed for an older advert or probe, since the message no longer matches.

## Agreements and the plan

A neighbour is never used without an **agreement**. The owner records it
with `ztm_agreement_set(p, id, cap_kbps)`, and it caps what this home may
draw from that neighbour. A neighbour's budget for a plan is
`min(offer, agreement)`. A neighbour is **eligible** for a destination
when all of these hold:

- its advert is current;
- its own WAN is up;
- the evidence for that destination is TRUE and not held;
- the medium is not amateur radio;
- there is an agreement.

`ztm_neighbour_view` gives the reason when a neighbour is not eligible.

`ztm_plan_make(p, dest, now, local_wan_up, demands, n, &plan)` builds the
plan in two steps:

1. **Choosing a relay.** Each flow goes to exactly one eligible neighbour:
   the one that would give it the largest max-min fair share, given the
   flows already placed (smallest demands are placed first). Ties go to
   the larger budget per flow, then to lower latency, then to the lower id.
2. **Water filling.** Each neighbour's budget is split max-min fairly among
   its flows, in exact rationals. No flow gets more than it asked for,
   and the leftover is shared equally among the flows that want more.

Every allocation is charged to that neighbour's libzxv rmag bandwidth
budget, which refuses any overdraw. The previous plan's charges are
released first. `ztm_budget_remaining` reads what is left, exactly. With
the local WAN up, the call returns `ZTM_E_WAN_UP` and no plan. With
nothing eligible, it returns `ZTM_E_NO_RELAY`.

## Example

```c
ztm_planner_create(cap, "no7", owner_pk, qlog, &p);
ztm_neighbour_add(p, "no3", ZTM_ALG_ED25519, pk3, 32);
ztm_agreement_set(p, "no3", (zind_qty){6000, 1});         /* at most 6 Mbit/s */
ztm_advert_accept(p, advert_bytes, advert_len, now);       /* signed by no3 */

ztm_demand d[2] = {{"tv", {8000, 1}}, {"phone", {500, 1}}};
ztm_plan_make(p, "internet", now, 0, d, 2, &plan);         /* tv 5500, phone 500 via no3 */
```

`make demo` runs [`examples/street_demo.c`](examples/street_demo.c). Home
"no7" loses its WAN. Three neighbours publish signed adverts (two
Ed25519, one ML-DSA-87). A fourth offers an amateur-radio relay, which is
refused, and a replayed old advert is refused too. One neighbour claims the
internet while no7's probe through it fails, so it is held as BOTH until
the owner signs USE. The eligible bandwidth is then shared among four
flows. A neighbour with no agreement is never used.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/tri-modem test     # 2,904 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/tri-modem SANITIZE=1 OUT=build/san test
make -C sdk/industry/tri-modem fuzz FUZZ_ITERS=20000
```

`test_modem` covers these areas:

- setup refusals;
- sealing and parsing with both algorithms: every malformed field, every
  truncation and every byte flip;
- acceptance: replay, older sequence, expiry, future dating, forgery,
  wrong algorithm, amateur radio and unknown neighbour;
- evidence: contradictory claims and probes held as BOTH, stale probes,
  owner resolution, forged resolution, and the override dropped at the
  next advert;
- the plan: a worked example in exact rationals, every reason a neighbour
  is not eligible, and the rmag charges;
- **simulated streets** of seven routers, each with its own planner and
  every other router as a neighbour. WAN links go down and come back at
  random, every router publishes, and every router that is down plans.
  Every plan is checked for budgets and for max-min fairness.

`fuzz_modem` runs random operations from five neighbours (Ed25519 and
ML-DSA-87) against an independent model of the evidence rules:

- adverts that are fresh, replayed, older, expired, dated ahead, forged or
  amateur-radio, with claims that are absent, true, false or contradictory;
- probes;
- genuine and forged owner resolutions;
- agreements changed and withdrawn;
- plans with random demands.

After every operation, each neighbour's view must equal the model's, and
every plan is checked for budgets, exact rmag charges and fairness. The
output is byte-identical under gcc and clang.

## Limits

- **Link selection only.** Tri-Modem says which neighbour to use and how
  much to draw. It does not route, tunnel, encrypt or shape traffic, and it
  touches no router configuration. All tests run on simulated topologies.
- **Amateur radio is never selected.** Under FCC Part 97 (47 CFR 97.113)
  and its equivalents elsewhere, amateur stations may not carry
  communications for pecuniary interest or on behalf of an employer, and
  may not obscure their meaning. There is **no settlement, payment or
  metering of any kind over amateur bands**. An advert whose medium is
  amateur radio is refused.
- **The owner's agreement is required.** No neighbour is used without one.
  Sharing a connection may also be restricted by the neighbour's ISP
  terms, which the kit cannot check.
- An advert proves that the neighbour's key signed it, not that the claim
  is true. Probes catch some false claims, and a contradiction is held.
  A neighbour that lies consistently and passes probes will still be
  used, up to the agreed cap.
- Bandwidth figures are what the neighbours offer, not measurements. The
  plan is exact arithmetic on those figures, and real throughput will
  differ.
- Times are the caller's clock. The planner is in memory (32 neighbours,
  64 flows per plan) and is not thread-safe.

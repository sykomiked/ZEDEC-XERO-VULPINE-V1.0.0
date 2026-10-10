<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Providers: selling compute, storage, memory and AI services on ZXV

`kernel/src/provider/` (prefix `prov_`) is the layer that lets anyone with spare or dedicated resources sell them through the ZXV network. That includes accelerator compute, CPU, RAM, storage, bandwidth, hosted model inference, fine-tuning, datasets and content pinning. The seller can be a large cloud, a university cluster, an AI lab, a decentralised GPU or storage network, or one person's workstation. It is freestanding integer C11 like the rest of the kernel and is covered by three host tests and a freestanding build check.

**This document describes how any provider could take part. ZXV has no partnership, endorsement, agreement or connectivity with Google, Anthropic or any other company.** Names of API styles in this document ("messages-style", "chat-style") describe common public request shapes, not particular vendors.

## Why a large provider would plug in

The design aims to make joining low-risk and low-cost for a provider. Each item below is enforced in code, not only stated as policy.

| What the provider gets | How it is enforced |
|---|---|
| **Keep your own stack.** Your endpoint, API, keys, terms of service and prices stay yours. | The adapter (`prov_adapter.h`) renders the network's request envelope into the request shape *you* declared and reads usage back through the response paths *you* declared. The kernel never stores provider API keys and opens no connection. |
| **No resale without you.** | Every listing (ask) must be signed with the ML-DSA-65 key that signed your descriptor (`prov_ask_post`). Nobody else can list your service. The network routes and settles; it does not resell. |
| **You set the price.** | Asks carry your unit price. Matching is price-time priority and fills execute at **your ask price**. |
| **A small, flat, published fee.** | The default fee is the platform's 0.08889% assurance fee, `floor(a·8889/10^7)` (`pay_assure_fee`), charged once on settled gross and split exactly into the four fee buckets (50% reserve floor (backs Vino, plus every remainder unit), 25% V-Bill dividend pool, 15% infrastructure/node bounties, 10% regenerative capital). An operator may publish a flat basis-point rate instead, but never above 5% (`PROV_FEE_MAX_BPS`). There are no listing, exit or data fees. The fee schedule's hash is inside every receipt, so it cannot change unnoticed. With the default fee you keep more than 99.9% of gross. |
| **No lock-in.** | `prov_provider_leave` works at any time. It withdraws your asks and returns unstarted user reservations in full, and there is no exit fee. Your co-signed receipts are portable signed objects, and `prov_rep_rebuild` re-verifies them on any other node to rebuild your reputation there. You can also come back later by re-registering with a higher descriptor version. |
| **Verifiable metering.** | You and the user both co-sign each usage receipt with ML-DSA-65. Disputes are resolved only against co-signed receipts. |
| **Anti-monopoly cap, not a ban.** | In each market and cycle, one provider may fill at most `max(⌊D/φ²⌋, ⌈D/P⌉)` units. Large providers can list freely. With no competitors you may fill all demand; with competitors, demand beyond your cap goes to the next asks. |
| **Reputation you can carry.** | Your reputation is a Laplace-smoothed SLA success rate over co-signed receipts. New providers start at a neutral 0.5, so they are not buried. |
| **Web 2 and Web 3 alike.** | One descriptor serves both. Chain adapters cover EVM, Bitcoin-family, Ed25519, Cosmos, Substrate (through an external signer) and any other chain through hooks. |

## Why it is good for the user

- **Price ceiling and budget cap per job** (`max_unit_price`, `max_total`). A fill never exceeds either.
- **Filters:** data-residency regions, legal jurisdiction, licence (commercial use allowed, SPDX allowlist), attested hardware only, minimum SLA availability, minimum reputation, Web 2 / Web 3.
- **Privacy flags.** `no_train` (no training on my data) and `no_retain` travel in the request envelope. They are routed only to providers whose signed descriptor declares that it honours them, are emitted as the provider's own declared header, and are recorded in the co-signed receipt. Dropping a flag from a receipt makes settlement fail as `PROV_ERR_TAMPER`.
- **No usury.** There is no interest, no time-based charge on balances and no late fee anywhere. `prov_charge_check` refuses those charge kinds outright, and the settle path also runs `pay_usury_check`. The only charges are usage (never more than what was held) and the network fee.
- **SLA credits.** If a receipt records an SLA breach, the provider's own declared credit (`credit_bps`) is returned to the user out of the gross, before the fee is taken.
- **Pay only for metered units.** A reservation is held when the job is matched. On settlement the provider receives payment for delivered units only, and the rest of the hold goes back to the user. A receipt signed by only one side moves no money.

## The pieces

| File | What it does |
|---|---|
| `prov.h`, `prov.c` | Descriptor, registry, attestation slots, signed asks, user bids, filters, price-time matching under the share cap, the fee, the no-usury guard |
| `prov_receipt.c` | Canonical receipt (330 bytes), digest, verification, settlement, disputes, reputation (Sybil rules), export and rebuild |
| `prov_pq.h/.c` | ML-DSA-65 (FIPS 204, `kernel/src/pqsec`) as the verify hook; signing helpers. Context string `"zxv-prov"` |
| `prov_adapter.h/.c` | Request envelope codec; JSON bodies for messages / chat / completion / embeddings / raw; response parsing through declared key paths (uses `kernel/src/web4` JSON) |
| `prov_chain.h/.c` | Chain adapters, receipt Merkle anchors, chain-asset conversion |
| `prov_pay.h/.c` | Settle hook over `pay_ledger` (DEBIT / CREDIT / EQUITY rails through `PAY_RAIL_*_CODE`) |
| `prov_capmkt.h/.c` | Bridge to the capacity market (`kernel/src/capmkt`) |
| `prov_swarm.h/.c` | A remote provider as one of the swarm's models inside the tokens-per-cycle budget |

### The provider descriptor

A descriptor (`prov_desc_t`) is signed with the provider's ML-DSA-65 key. The provider id is SHA3-256 of the public key. A descriptor declares:

- **Identity:** name, network (`PROV_NET_WEB2` / `PROV_NET_WEB3`), native chain family, chain id and payout address (required for Web 3), and the legal entity's jurisdiction.
- **Regions** (ISO 3166-1 alpha-2) where each offer runs.
- **SLA tiers:** availability in ppm, maximum latency, and service credit in basis points.
- **Offers** (up to 8):
  - resource class: `ACCEL`, `CPU`, `RAM`, `STORAGE`, `BANDWIDTH`, `INFERENCE`, `FINETUNE`, `DATASET`, `PINNING`
  - unit: tokens, accelerator-seconds, core-seconds, GiB-seconds, GiB-hours, GiB, requests, records
  - accelerator type, count and memory
  - capacity per cycle
  - licence: SPDX id, commercial-use flag, hash of the provider's terms
  - API declaration: request shape, model name, response paths for input / output usage and text, and privacy header name
- **Privacy commitments:** `honours_no_train` and `zero_retention`.
- **Attestation slots** (filled after registration). TEE, confidential-VM, accelerator confidential-compute, TPM and organisational-identity evidence are carried as **opaque blobs** (their SHA3-256 is stored). Only the operator's `attest` hook may mark a slot `VERIFIED`. Without a hook, evidence stays `UNVERIFIED` and never satisfies `require_attested`. This code does not claim to verify any vendor's attestation format itself.

Re-registering requires a higher `version`. It withdraws live asks so that terms cannot change under an open listing, and it clears attestation, because the evidence was bound to the old descriptor. Reputation and receipts are kept.

### Matching

`prov_match(net, rclass, unit, asset)` clears one market:

1. Bids are ordered by ceiling (highest first), then arrival. Asks are ordered by price (lowest first), then arrival.
2. For each bid, the matcher walks the asks within its ceiling that pass the bid's filters. It takes `min(want, ask left, cap room, budget / price)`, executes at the ask price and places a `HOLD` through the settle hook. If the user cannot fund the hold, no fill is made.
3. **Cap.** `D` is this cycle's demand in the market: units already matched plus open eligible bids. `P` is the number of providers with live asks or fills in the market this cycle. The cap is `max(⌊D·cap_q32/2³²⌋, ⌈D/P⌉)`, where `cap_q32` defaults to `PROV_CAP_INV_PHI2_Q32 = ⌊2³¹(3−√5)⌋ = 1640531526` (1/φ², 38.2%). The test recomputes this constant with an exact integer square root. Demand that no provider under its cap can serve stays open as an uncharged bid for later cycles.
4. All-or-none bids (`allow_split = false`) are filled whole by one ask, or not at all.

There are two clearing modes, and both use one registry:

- **`prov_match`**: continuous, at the ask price, in any quote asset (VFV, ISO 4217, chain assets), with every user filter.
- **`capmkt`**: another module's periodic uniform-price double auction for plain VFV commodity markets. `prov_capmkt` feeds it rather than duplicating it:
  - A provider's signed ask moves into capmkt's book and leaves prov's, so it is never sold twice.
  - A user's job is mirrored as a bid.
  - capmkt's fee hook is routed to `prov_fee`, so the fee is the same in both modes.
  - capmkt's delivery-proof verifier accepts a proof only when its evidence is the digest of a prov receipt that **both** parties co-signed. Each receipt pays one proof.

### Receipts and settlement

`prov_receipt_build` turns metered usage into a receipt. The receipt carries:

- job id, provider id, user id, descriptor hash, fee-schedule hash, request hash and response hash
- class, unit, SLA outcome and flags
- asset
- units, unit price, gross, SLA credit, fee, net, refund and hold
- latency and ticks

The body is a fixed 330-byte little-endian layout, documented in `prov_receipt.c`. Its digest is `SHA3-256(le32(19) || "zxv-prov-receipt-v1" || body)`. An independent Python encoder (`gen_prov_chain_vectors.py`) reproduces the digest byte for byte.

Both sides sign the digest. `prov_settle` then checks the following, in order:

- the receipt describes exactly this fill (job id, parties, descriptor, request hash, price, hold, class, unit, privacy flags, declared SLA credit)
- the arithmetic holds: `gross = units × price`, `fee = prov_fee(gross − credit)`, `hold = net + fee + refund`
- the fee schedule is the current one
- both signatures verify

If those checks pass, it posts `FINAL` through the settle hook. Settlement is idempotent.

| Case | Result |
|---|---|
| Body altered, or receipt for another job | `PROV_ERR_TAMPER`; nothing changes |
| A signature missing or invalid | Fill becomes `DISPUTED`, no money moves (`PROV_ERR_AUTH`) |
| Dispute resolution | `prov_dispute_resolve` accepts only a receipt both parties signed, such as an amended one. It can record the dispute as lost by the provider. |
| Settle hook refuses | `PROV_ERR_SETTLE`; state unchanged |

`prov_pay` maps the hook onto `pay_ledger`. **HOLD** moves funds from user to escrow. **FINAL** is one atomic posting from escrow to provider (net), commons (fee) and user (refund). **RELEASE** moves funds from escrow back to the user. UETR, end-to-end id and idempotency key are derived from the receipt digest, so a replay is a ledger `PAY_DUPLICATE` and never pays twice. VFV and any ISO 4217 currency use the same rails.

### Reputation and Sybil resistance

`score = (met + 1)·2¹⁶ / (met + breached + 2·lost + 2)`.

A settled receipt counts toward reputation only if all of these hold:

- the counterparty passes the configured rule: `PROV_SYBIL_NONE`, `STAKE` (stake ≥ `min_stake`), `ATTESTED`, or `EITHER`
- the user's key is not the provider's key (self-dealing pays the fee but earns no reputation)
- that user has counted fewer than `rep_max_per_user` receipts for this provider this cycle

`prov_rep_export` lists receipt digests. `prov_rep_rebuild` recomputes reputation from the receipts themselves on any node. It ignores duplicates, forgeries and receipts signed by the wrong key.

### The swarm

The swarm splits a fixed token budget per cycle across its models under the Fibonacci rule (`swarm_budget.h`). `prov_swarm_attach` registers a remote inference offer as an ordinary model slot at a chosen level, so it receives its share exactly like a local model. `prov_swarm_request` charges the request against that allotment first, then posts a bid for the granted tokens only. The bid carries the user's ceiling, filters and privacy flags. Unused tokens expire at the end of the cycle (R6), as they do for local models.

## Web 3 and chain adapters (`prov_chain.h`)

Decentralised compute, storage, pinning and inference networks list through the same descriptor. The chain adapters bind a chain identity to receipts, anchor receipt batches on a chain, and convert chain-asset amounts. All primitives come from modules already in the tree: `kernel/src/web4` (Keccak-256, RLP, EIP-1559, secp256k1 sign/verify/recover, EIP-55, ABI, HASH160, bech32/bech32m, segwit), `kernel/src/ipfs_node` (base58), `kernel/src/robin_debanks` (SHA-256, Ed25519 verify) and `kernel/src/mlkem` (SHA3-256).

| Family | Signature over a receipt | Address | Anchor payload |
|---|---|---|---|
| EVM | EIP-712 typed data `ZXVReceipt(...)` under domain ("ZXV Provider Receipt", "1", chainId[, contract]); 65-byte r‖s‖v, low-s, recovered address must match | EIP-55 | `anchor(bytes32)` calldata; `prov_evm_anchor_tx` builds the EIP-1559 transaction |
| UTXO (Bitcoin family) | signed-message hash (double SHA-256 of `"\x18Bitcoin Signed Message:\n"‖varint‖text`), 65-byte compact recoverable signature, bound to a P2WPKH/P2PKH key hash | bech32 / bech32m segwit | `OP_RETURN PUSH32 root` |
| Ed25519 (Solana-style) | Ed25519 over the receipt text | base58 of the 32-byte key | memo `zxv1:<hex>` |
| Cosmos / Tendermint | secp256k1 over SHA-256 of the text, 64-byte r‖s, low-s, compressed key | bech32 (chain HRP) of HASH160 | memo `zxv1:<hex>`; `prov_cosmos_sign_doc` encodes the protobuf `SignDoc` (SIGN_MODE_DIRECT, no amino JSON) |
| Substrate | **external**: sr25519 is not implemented here; the host's `ext_verify` hook is required, otherwise `PROV_ERR_UNSUPPORTED` | opaque (SS58 not decoded) | `zxv1‖root` for the host's extrinsic builder |
| Opaque | host hooks | opaque | `zxv1‖root` |

The receipt text signed by non-EVM keys is `"zxv-prov-receipt:" ‖ hex(receipt digest)`.

**Anchoring.** Receipt digests form a SHA3-256 Merkle tree: leaf = `H(0x00‖d)`, node = `H(0x01‖l‖r)`, and an odd node is promoted. Any chain can carry the 32-byte root. Anyone holding a receipt and its path (`prov_anchor_proof` / `prov_anchor_verify`) can check inclusion against the anchored root.

**Payment in a chain asset.** The conversion uses a posted rate, given as quote minor units per whole token. `prov_chain_to_quote` computes `floor(amount × price / 10^decimals)` and `prov_quote_to_chain` is its inverse. Both are exact and floor on 256-bit values, so neither side is ever over-paid by rounding. Settlement then runs over the pay rails like any other payment, with no interest.

**No live connectivity.** The adapters encode, hash, sign and verify. Submission goes through the host's `submit` hook. secp256k1 and Ed25519 are classical algorithms. The ZXV side of every receipt is also co-signed with ML-DSA-65, so chain signatures are bindings, not the root of trust.

## How a provider would join (any provider)

1. Generate an ML-DSA-65 key (`prov_pq_keygen`, or an HSM through the `verify` hook's counterpart).
2. Fill a `prov_desc_t` with your offers, regions, SLA tiers, licences and API shape, then sign it (`prov_pq_sign_desc`) and register it (`prov_register`).
3. Optionally present attestation evidence (`prov_attest_present`). The operator's verifier decides whether it counts.
4. Post signed asks at your prices (`prov_pq_sign_ask` + `prov_ask_post`). Re-post each cycle to change price or quantity.
5. Serve matched jobs through your existing endpoint. The host's transport sends the adapter's body with the user's own credentials.
6. Co-sign receipts with the user (`prov_pq_sign_receipt`). Settlement pays your net into your pay-ledger account, or through capmkt for VFV commodity markets.
7. You can leave at any time (`prov_provider_leave`) and export your receipts.

## Tests

The three host tests and the freestanding check run from `kernel/`. The `verify-all` recipe lines are provided as a fragment; the `make` line below assumes that fragment has been pasted into `kernel/Makefile`.

```
make verify-all     # includes the provider stages
```

| Test | Checks | Covers |
|---|---|---|
| `test_prov` | 150 | 1/φ² constant from an integer square root; cap arithmetic; fee math (0.08889% assurance fee and bps, 5% ceiling, schedule hash); no-usury guard; assets (rail numerics never ISO 4217); signed descriptors and tamper; attestation states with and without a hook; non-resale and replayed asks; matching with the cap, ceilings, budgets, all-or-none, price-time priority, unfunded holds; every filter; receipts with real ML-DSA-65 (encode/decode, half-signed disputes, tamper cases for units, re-pricing, fee, fee schedule, privacy flags, job id, swapped signatures, response hash); SLA credit; hook refusal; Sybil rules, per-cycle limits, self-dealing; export and rebuild; leaving and returning; adapter envelope round trips for every shape, exact JSON bodies, privacy header, response parsing via declared paths, malformed, negative and duplicate-key responses |
| `test_prov_chain` | 62 | EIP-712 specification "Ether Mail" vectors (domain separator, struct hash, digest, signature recovery); EVM address of key 1 and EIP-55; typed-data receipt signatures with tamper, cross-chain and high-s cases; EIP-1559 anchor transaction round trip; BIP-173 P2WPKH vector; RFC 8032 TEST 1; base58 all-ones system address; Cosmos SignDoc wire bytes; cross-implementation receipt digest, Ed25519, Cosmos and Bitcoin signatures generated by `gen_prov_chain_vectors.py` (Python `cryptography`); external-signer families; Merkle anchors for n = 1..9; exact asset conversion |
| `test_prov_bridge` | 33 | `pay_ledger` settlement in VFV and EUR with ledger invariants, chain verification and idempotent replay; capmkt clearing, receipt-backed delivery, one receipt per proof, conservation; swarm Fibonacci budget for a remote model |

The freestanding check compiles every `prov*.c` file with `clang --target=aarch64-none-elf -ffreestanding -std=c11 -O2 -Wall -Werror -Wextra` and fails on any libc or libgcc symbol in `nm -u`. A 32-bit ARM build also shows no `__aeabi_uldivmod`: 64-bit quotients go through `zt_udiv64` / `pay_muldiv`.

## Honest limits

- **No connectivity.** Request delivery and chain submission are host hooks; nothing here talks to any provider or chain.
- **Attestation.** Evidence is carried, not interpreted. Verification is the operator's hook, and no vendor format is parsed here.
- **Matching scope.** Matching is per node over the order set it holds. Agreeing on one order set across nodes needs a consensus or gossip step outside this module, as for capmkt.
- **Reputation and Sybil resistance.** Reputation measures co-signed receipts and SLA outcomes, not output quality. Sybil resistance is only as strong as the configured stake or identity source; one entity holding many staked identities can still split supply across them.
- **sr25519 / SS58.** These are not implemented; the Substrate family is external-only.
- **Chain cryptography.** secp256k1 and Ed25519 are classical. The secp256k1 code is web4's and is unaudited (see `web4_web3.h`).
- **Reentrancy.** `prov_adapter_parse`, `prov_anchor_*` and `prov_rep_rebuild` use static scratch buffers, and the descriptor digest uses an 8 KiB stack buffer. Call them from one thread at a time.
- **Law.** Consumer protection, data protection, export control, tax, sanctions and financial-services law vary by jurisdiction. They are for the operator and counsel.

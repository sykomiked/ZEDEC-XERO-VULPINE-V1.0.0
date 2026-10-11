<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# zenny: Zenny chips (user-built compressed baskets)

**Trademark note.** "Zenny" is a currency name in Capcom's Mega Man / Breath
of Fire games; the name needs a trademark check before any public use. Code
identifiers keep the `zenny_` prefix so a rename is a mechanical
search-and-replace.

A Zenny chip is a basket with a mandatory V-Bill anchor leg plus weighted
fiat, commodity and compute voucher legs, recorded in a WORM (hash-chained)
manifest. This directory is the exact manifest and accounting core; posting
the leg movements on a ledger is the caller's job. The rules Z1 to Z6 are in
`zenny.h`:

- **Z1 manifest**: chip id = SHA-256 of the canonical mint content; kappa
  (reference units per chip) and rules are fixed at mint; every leg value
  `qty * num / den` must divide exactly.
- **Z2 exact claim**: `chips * kappa == sum of leg values` and every weight
  matches its value share exactly. A V-Bill leg with weight > 0 is
  mandatory. Commodity legs need a custodian attestation; bilateral chips
  need both signatures. Margin (leverage) and credit claims are refused.
- **Z3 WORM**: every version is chained; `zenny_verify_chain` recomputes it.
- **Z4 rebalance**: only when allowed at mint, signed, claim unchanged.
- **Z5 decompress (tap-to-pay)**: exact payouts per leg, valued at the
  caller's batch prices, with the 0.08889% fee (`pay_assure_fee`) recorded
  in a disposal receipt. Overdraft fails closed; a full receipt ring
  refuses rather than overwrite records.
- **Z6**: ledger posting is the caller's.
- **Z7 top-up** (only with `ALLOW_TOPUP`): adds value by buying into the
  existing legs in exactly the current weights, only into backed legs
  (V-Bill, fiat, and commodity legs with a standing, unexpired custodian
  attestation; compute vouchers are never backed). It mints
  `amount / kappa` new chips to a recipient (kappa never changes) and is
  appended to the chain. `zenny_raffle_credit` adapts a raffle's
  basket-NAV credit (dest 3) to it.

**Honest limits.** A chip is a claim on its legs, not new money; compression
changes density only. An attestation proves who signed, not that goods
exist. Whether a chip is a security, e-money or a collective investment
scheme depends on jurisdiction. Designed to support central-bank co-minting;
not connected to any central bank.

Test: `test_zenny.c` (see the `test-zenny` command in the module report).

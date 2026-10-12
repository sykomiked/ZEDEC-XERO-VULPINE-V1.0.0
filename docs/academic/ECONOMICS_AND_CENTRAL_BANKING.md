<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The ZXV internal economy: mechanism design, ledger invariants and monetary architecture

*A technical report for economists, monetary economists and central bankers.*

## Abstract

ZXV is an operating system whose kernel contains a set of accounting state machines written in freestanding C11 with integer-only arithmetic. They allocate compute among AI agents, record value exchanged inside the system, and model payment and settlement designs familiar from central banking. This report describes those mechanisms in the vocabulary of mechanism design and monetary economics. It covers: a three-rail double-entry ledger whose balance identity is checked after every posting and reconciled against a second book every cycle; a fixed, non-compounding 0.08889% assurance fee with an exact four-way partition; a reserve floor and a capped solvency vault; primary issuance of an equity-like internal claim ("V-Bill") by descending-clock uniform-price auction epochs, constrained so that issuance never takes the reserve below its floor; haircutted, custodian-signed collateral receipts; a no-usury filter; and a proportional-share market for inference tokens with anti-concentration caps. Each statement is tied to source files and tests. The software is accounting code. It is not a licensed bank, payment institution, securities offering or investment. We separate what is tested from what is design intent, and we list open questions. These include the incentive properties of the auctions, which have not been analysed formally.

## 1. Introduction and scope

The economy has two purposes. The first is to budget a scarce resource: tokens generated per cycle by a swarm of local AI models ([`docs/SWARM_ECONOMY.md`](../SWARM_ECONOMY.md)). The second is to record value that people exchange inside the system, for example in the marketplace ([`docs/COMMERCE.md`](../COMMERCE.md)). The authors chose monetary rules because their invariants are well understood: balances add up, nothing is created from nothing, and every movement leaves a record. Plain-language background is in [`docs/supplemental/ECONOMY_WHAT_IT_IS_AND_IS_NOT.md`](../supplemental/ECONOMY_WHAT_IT_IS_AND_IS_NOT.md) and [`docs/supplemental/TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md`](../supplemental/TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md). This report does not repeat them.

**Scope.** Sections 3 to 12 describe mechanisms in `kernel/src/{pay, settle, swarm, svault, vauction, vswap, vbond, vderiv, zenny, sicily, zcapital, cbank, iso20022}`. Rule labels (L1, F2, A4, and so on) are the labels used in each header file. All quantities are unsigned integers in minor units unless stated otherwise. Divisions are floors unless stated otherwise, and the code documents where each remainder goes. The repository state described here is commit `0888621` on `main`.

**Notation.** $\lfloor x \rfloor$ is the floor. For a ledger account $a$, $D_a$, $C_a$ and $E_a$ are its DEBIT (rail 555), CREDIT (rail 777) and EQUITY (rail 888) balances. $N$ is the number of V-Bills outstanding, $R$ is the reserve value, and $h$ is the number of V-Bills held by the solvency vault.

## 2. Legal and regulatory status (read first)

- **Accounting software, not a financial institution.** ZXV is not a bank, payment institution, money transmitter, e-money issuer, card scheme, exchange, central counterparty or custodian. It holds no licence to act as any of these.
- **Not a securities offering or an investment.** "V-Bill", "V-Bond", "V-Call", "V-Perp", "dividend pool" and "Zenny chip" name internal accounts and state machines in source code. No instrument is offered to anyone. The headers say that several of these would likely be securities in many jurisdictions (`svault.h`, `vauction.h`), and that derivatives belong to the most heavily regulated class (`vderiv.h`). The issuance-adjacent modules `vswap`, `vbond` and `vderiv` ship switched off and need a signed per-jurisdiction manifest to enable them.
- **VFV is not legal tender or a deposit.** VFV is the platform's internal unit (numeric 555, flagged non-ISO in `pay_ledger.h`). The writers never put VFV in an ISO 20022 `Ccy` field ([`docs/CENTRAL_BANK.md`](../CENTRAL_BANK.md)).
- **No warranty.** The code is provided as is under the Apache License 2.0. Schema validity and passing tests show internal consistency. They do not show regulatory compliance, settlement finality, or conformity with the CPMI-IOSCO Principles.
- **Real-world operation requires licensing.** Moving funds, issuing cards, issuing anything security-like, operating a reserve or fee pool, and running a raffle are regulated activities in most jurisdictions. Before any of them, the operator needs local legal review and the relevant licences. `docs/ARCHITECTURE.md` §18 lists these legal questions as open.
- **What exists.** Section 13 classifies each claim. In short:
  - The ledger, the fee, the settlement spine and the swarm market are on `main`, tested in CI and linked into the hosted application (`kernel/arch/hosted/zxv_host.c` calls `settle_init`, `settle_levy` and `settle_sync`).
  - The vault, the auction, the swap, the bond, the derivative, the basket and the tier modules are on `main` and tested in CI through `make -C kernel verify-all` → `test-econ`. **No application path calls them.**
  - PAPSS interoperability is design intent only.

## 3. The ledger of record and the balance identity

**Definition 3.1 (posting).** A posting is a set of at most 8 lines. Each line names an account, a DEBIT delta $\delta^D$ and a CREDIT delta $\delta^C$. The EQUITY delta is derived as $\delta^E = \delta^D - \delta^C$ and posted on every line (`pay_ledger.h`). Only accounts flagged ISSUER may carry $C_a > 0$, so holders cannot have debt.

**Invariants** (checked by `pay_ledger_check` after every operation):

- **L1 (per posting).** For every pair (asset, capital form) $g$: $\sum_{\ell \in g} \delta^D_\ell = \sum_{\ell \in g} \delta^C_\ell$.
- **L2 (global).** For every $g$: $\sum_a D_a = \sum_a C_a$ and $\sum_a E_a = 0$.
- **L3 (per account).** $E_a = D_a - C_a$, $D_a, C_a \ge 0$, and balances stay below $2^{62}$.
- **L4.** Postings are all-or-nothing.
- **L5.** Crown-form accounts (Section 11) move value only between accounts of the same owner.

**The identity in this notation.** Read DEBIT as assets held and CREDIT as claims outstanding. Then L3 is the accounting identity $A = L + E$ for each account. L2 says that in each closed group every asset held by a holder is matched by an issuer's liability. Net equity across the group is therefore zero. A system made only of inside claims should satisfy exactly this. The Sicily Account view (`sicily.h`, S1) shows an owner's totals as Assets, Liabilities and Equity. It derives $E = A - L$ and never stores it.

**Provenance.** Every posting appends a SHA3-256 hash-chained record: previous hash, request digest, UETR, end-to-end id, attestor, tick and lines. Replay protection (R1 to R3) uses unique UETRs and idempotency keys inside a journal window, plus an optional strictly increasing per-initiator nonce.

**Two-book reconciliation per cycle.** The swarm market keeps its own conserved money table (rule M8 in `swarm_market.h`). The settlement spine (`settle.h`) posts the differences to `pay_ledger` as balanced two-line transfers (S3) and then reconciles the two books:

- **R1 (before posting).** Holdings plus pot plus levied equals money supply equals amount issued.
- **R2 (after posting).** Every model's balance, the pot account and the four buckets match.

On any mismatch the spine halts (R3). It never edits either book to make them agree. An optional peer-audit hook replays each transfer on a replica and checks the state hash (A1 to A3; [`docs/PEER_AUDIT.md`](../PEER_AUDIT.md)).

**Model checking.** [`docs/FORMAL_INVARIANTS.md`](../FORMAL_INVARIANTS.md) states P1 to P11 for `pay_ledger_post`. TLA+ models of these are checked by TLC over small bounds. [`proofs/README.md`](../../proofs/README.md) reports, for example, that `MC_ledger_conservation.cfg` gives "31,134 states generated, 951 distinct, depth 6" with no error. P6 (no int64 overflow at the real widths) is a Lean lemma over all values. The models are hand transcriptions with no refinement proof to the C code, so this is not verification of the kernel. Two negative results are documented:

1. Without a nonce, a request is accepted again once it leaves the journal window (`MC_ledger_replay_wrap.cfg`).
2. In the older `finance/triple_ledger`, "trial balance always zero" is false, because voucher redemption is a one-sided debit (`MC_triple_ledger_trial.cfg`).

## 4. The assurance fee

**Definition 4.1.** For a gross amount $g$:

$$\mathrm{fee}(g) = \left\lfloor \frac{8889\, g}{10^7} \right\rfloor \quad (0.08889\%)$$

The product uses a full 128-bit computation for every $g < 2^{64}$ (`pay_assure.h`, F1). For example, $\mathrm{fee}(1124) = 0$ and $\mathrm{fee}(1125) = 1$.

**Proposition 4.2 (carry; F2).** Keep a per-account remainder $r \in [0, 10^7)$, and for each charge set $x = 8889g + r$, fee $= \lfloor x/10^7 \rfloor$ and $r' = x \bmod 10^7$. Then any sequence $g_1, \dots, g_n$ starting from $r = 0$ pays exactly $\lfloor 8889 \sum g_i / 10^7 \rfloor$. Splitting a payment into micro-payments therefore cannot avoid the fee.

**Non-compounding (F3).** The fee applies once to the gross of a transaction. It never applies to a fee or to itself. A sale released in parts is charged fee(cumulative) minus fee already charged. The owner set one fee for all participants, with no staker discounts (`docs/ARCHITECTURE.md` §17).

**Definition 4.3 (partition; B1).** Write $p = (50, 25, 15, 10)$ for the reserve floor, the V-Bill dividend pool, infrastructure and node bounties, and regenerative capital. Then $\mathrm{out}_i = \lfloor \mathrm{fee} \cdot p_i / 100 \rfloor$ for $i \ge 1$, and $\mathrm{out}_0 = \mathrm{fee} - \sum_{i \ge 1} \mathrm{out}_i$. Every remainder unit goes to the reserve floor, so the parts sum exactly to the fee. Example: a price of 1,250.00 VFV is 125,000 minor units and gives a fee of 111 minor units (1.11 VFV, as in [`docs/TWO_NODE_DEMO.md`](../TWO_NODE_DEMO.md)). The split is $(57, 27, 16, 11)$.

In the swarm, `settle_levy` charges the fee once per cycle on the pot, keeping the carry. The node bounty bucket is returned to the pot first (settle F1 to F3; swarm M10). The reserve, dividend and regenerative buckets keep what they hold.

## 5. Reserve floor and solvency vault

**Definition 5.1 (floor).** For a floor ratio $f = f_n/f_d$ and backing $b$ (fiat minor units per V-Bill), the reserve satisfies its floor when

$$R \cdot f_d \ge f_n \cdot b \cdot N$$

This is compared exactly in 128-bit arithmetic (`vauction.h`, A3).

**The vault** (`svault.h`) gets one third of each reserve-floor share. That is 16.67% of the fee, swept with an exact carry (S1): $3(\text{swept} + \text{overflow}) + r = \text{share\_total}$, with $r < 3$. The rules are:

- **S2 (cap).** Holdings $h \le \lfloor N/3 \rfloor$. Past the cap, the swept part stays in the reserve.
- **S3 (buys).** The vault buys V-Bills only with swept fee cash, at a caller-supplied price.
- **S4 (solvency test).** `zxv_verify_vault_solvency` excludes vault holdings from the redemption base: $R f_d \ge f_n (N-h)\, b$. The floor per V-Bill is the rational $R/(N-h)$. A V-Bill the protocol owns is not a claim on the reserve. This exclusion is an anti-reflexivity rule.
- **S5 (first loss).** The vault absorbs a loss first: it redeems up to $h$ of its V-Bills at par and reports the rest as uncovered.

**Redemption at the ratio, not at par.** In `vswap.h` (W2 to W4), swapping out pays $\lfloor vR/B \rfloor$ with $B = N - h$, and swapping in charges $\lceil v\,r_n/r_d \rceil$. Neither direction lowers the ratio $R/B$. The header says that the value received "can be LESS than the user paid in". Losses are therefore shared pro rata. Section 15 relates this to bank-run theory.

**A disagreement between modules.** `vauction` and `svault` treat the reserve as a fiat account, while `vswap` treats it as a Vino account. On `main` the modules are tested separately against one `pay_ledger` and are not composed in the application, so which asset the production reserve holds is unresolved.

## 6. V-Bill primary issuance: backed-only descending uniform-price auctions

**Supply schedule (A4).** Let $k = f_n b$ and $c = p\, f_d$. The largest issue $q$ at price $p$ that keeps the floor solves $(R + qp) f_d \ge k(N + q)$:

$$Q(p) = \begin{cases} 0 & R f_d < kN \\ \infty & c \ge k \\ \left\lfloor \dfrac{R f_d - kN}{k - c} \right\rfloor & \text{otherwise} \end{cases} \qquad S(p) = \min(\text{cap}, Q(p))$$

$Q$ is non-decreasing in $p$. Issuing at or above the per-unit floor value never dilutes the reserve. Below that value, the slack $R f_d - kN$ bounds how much can be issued.

**Bids and clearing (A5, A6).**

- A bid is a pair (quantity, limit). Submitting it locks $q \times$ limit in escrow by a ledger transfer.
- The clock descends from $p_{\text{start}}$ in steps to $p_{\min}$. Demand is $D(p) = \sum_{\text{limit} \ge p} q$.
- The price is the first rung with $S(p) > 0$ and $D(p) \ge S(p)$, and the quantity issued is $S(p)$.
- Bids above the previous rung fill in full. Marginal bids share the rest pro rata by quantity, using floors, and leftover units go one each in submission order.
- Every winner pays the same price $p$.
- If no rung clears, the epoch issues $\min(D, S)$ at the lowest rung with $S > 0$.

**Settlement (A7).** One atomic posting per winner: the escrow is released, $q_i p$ goes to the reserve, the remainder is refunded, and the issuer's CREDIT and the bidder's V-Bills each rise by $q_i$. Auction fiat is never burned. Settlement re-values $R$ and $N$ first. If the issue would now break the floor, it refuses (`VAUC_ERR_FLOOR`), and the operator may then cancel the epoch with full refunds.

**Worked example** (`test_floor_binds` in `kernel/src/vauction/test_vauction.c`). Take $R = 50{,}000$, $N = 300$, $f = 1$, $b = 100$ and step 10. Then $Q(p) = \lfloor 20000/(100-p) \rfloor$. Bids are (400 at 90), (300 at 80) and (100 at 70).

- At 80: $S = 1000 > D = 700$, so this rung does not clear.
- At 70: $S = 666 \le D = 800$, so the epoch clears 666 units at 70. Fills are 381, 285 and 0.

Afterwards $R = 96{,}620 \ge 100 \times 966$. The issue stopped exactly at the floor.

**Neutrality.** The bid struct has no tier field, and ties break only on submission sequence (`vauction.h`). `sicily.h` S3 states that Player Card tiers carry no auction priority, fee discount or credit line.

## 7. Collateral: custodian-signed, haircutted warehouse receipts

The reserve value $R$ (A1) is the fiat reserve balance plus the sum over live receipts of

$$\left\lfloor v \cdot \frac{10^4 - \text{haircut}_{\text{bps}}}{10^4} \right\rfloor, \qquad v = \left\lfloor \frac{\text{qty} \cdot \text{median}}{\text{scale}} \right\rfloor$$

- **Signatures.** Each receipt is signed by a registered custodian over a fixed message (`vauc_receipt_msg`). The operator sets a haircut per custodian.
- **Price feed.** The median is the lower median of fresh quotes, each signed by a registered oracle. A quote must be newer than that oracle's previous quote and no older than `max_age`. Fewer than `min_quotes` fresh quotes value the receipt at zero, so valuation fails closed.
- **Disputes and expiry.** A disputed receipt is quarantined permanently. Expired receipts stop counting.
- **Signature verification** goes through a caller-supplied hook (ML-DSA, NIST FIPS 204, in production; the tests use a stub).
- **Stated limits.** The header says that a signature proves who signed, not that the goods exist or are unencumbered. It also says that a median resists a minority of bad quotes, not a majority.
- **Open decision.** Whether fee income covers a custodian default is open. The default is that the loss stays in the basket (`docs/ARCHITECTURE.md` §18).

## 8. The no-usury constraint

The project rule is that no charge grows with elapsed time on an outstanding amount (`cb_usury.h`; `pay_ledger.h`).

- **Refused by `cb_usury_check_fee` and related checks:** interest, coupons, discount accretion, negative rates, compounding, late-payment accrual and rollover fees.
- **Allowed:** flat or ad valorem fees per event; a one-off non-accruing penalty marked as charity or cost recovery; and profit-and-loss sharing booked on the equity rail 888. These follow AAOIFI categories (murabahah, ijarah, musharakah, mudarabah).
- **Enforcement by type.** The card module has no field that can hold a rate. A test leaves a statement unpaid for 1,000 days and checks that the amount owed does not change ([`docs/CARD_NETWORKS.md`](../CARD_NETWORKS.md) §3).
- **No rate fields.** V-Bonds and V-Calls/V-Perps have no rate field. Their only return is a share of realised revenue sitting in a pool (`vbond.h` B4) or a capped, fully collateralised payoff with no margin calls (`vderiv.h` V1, V2).
- **Limit.** The guard judges the description it is given and cannot detect usurious substance behind a flat-fee label.

## 9. Market structure: closed clearing and the optional batch auction

**Closed clearing** is the default market mode (`docs/ARCHITECTURE.md` §17, decision of 2026-10-10 04:04). Transfers of V-Bonds and derivative positions are bilateral signed hand-overs. There is no order book, no matching and no quote (`vbond.h` B2; `vderiv.h`). An open **batch auction** is recorded as a per-jurisdiction switch. Only primary issuance (Section 6) is built. **No secondary-market batch auction exists on `main`.** In this report, the batch auction is design intent.

## 10. The proportional-share token market among AI agents

Each cycle has a fixed token total $T$. Rules M1 to M10 in `swarm_market.h` divide it in four steps:

1. **Emotional pool.** An emotion-weighted pool of at most $8/21$ of $T$ comes off the top.
2. **Commons floor.** $8/21$ of the remainder is split by a Fibonacci level rule.
3. **Open market.** The other $13/21$ is sold by proportional share. Model $i$ bids $b_i$ of its Financial capital and receives $x_i = M\, b_i / \sum_j b_j$ tokens (largest remainder), so the price is $\sum_j b_j / M$.
4. **Settlement.** Winners pay their bids into a pot. After the cycle, $13/21$ of the pot is paid as income in proportion to verified value produced, and $8/21$ is a cooperation dividend in proportion to Social capital earned.

This is the Spawn allocation (Waldspurger et al., 1992), also known as Kelly's proportional allocation. Money is conserved (M8). Every split is recomputed by a different model, which acts as a witness ([`docs/SWARM_ECONOMY.md`](../SWARM_ECONOMY.md) §7). Unused tokens expire each cycle (`swarm_budget.h`, R6). On the compute budget this has the effect of full per-cycle demurrage, but it does not apply to VFV.

## 11. Nine forms of capital and no-monopoly caps

`zcap_forms.h` fixes nine forms: Financial, Manufactured, Intellectual, Human, Social, Natural, Cultural, Spiritual and System. Every module that indexes a form aliases this order, and `test_capital_canon.c` checks the aliases. The four "Crown" forms (Social, Natural, Cultural, Spiritual) are inalienable:

- they can be earned, but no call moves them between owners (M9; ledger L5);
- they cannot be bid.

The anti-concentration rules are:

- **M4.** No model wins more than $8/21$ of the market tokens. The excess is redistributed pro rata, and tokens nobody may take fall back to the commons.
- **M7.** No model holds more than $\max(8/21 \text{ of all money},\ \text{an equal share})$. The excess is shared equally among models under the cap.
- **Vinea peer settlement.** The same constants apply between peers, with a two-member exception ([`docs/VINEA.md`](../VINEA.md) §7).

These rules trade allocative efficiency for starvation-freedom and bounded concentration. No welfare analysis has been done.

## 12. Interoperability: ISO 20022, ISO 8583 and PAPSS-style netting

**ISO 20022.** Three modules write ISO 20022 messages:

- `pay_iso.h` builds `head.001`, `pacs.008`, `pain.001`, `pain.002` and `camt.052/053/054`, with strict parsers for inbound `pacs.008` and `camt.053`.
- `cb_mx.h` builds `pacs.002/004/009` and `camt.056/029`.
- `verify-all` validates the cbank output with `xmllint` against the base XSDs in `kernel/src/pay/xsd`.

`pay_iso.h` states that the older `kernel/src/iso20022` module's `pacs.008` output "is not schema-valid" and that `pay_iso` replaces it for external messages. Note that `sdk/libzxv` still calls `iso20022_pacs` and `iso20022_camt`. The card module implements a general ISO 8583:1987 subset ([`docs/CARD_NETWORKS.md`](../CARD_NETWORKS.md) §6).

**Multilateral netting** (`cb_net.h`). Participants are prefunded in their local currency at its central bank. Cross-currency legs convert through one settlement unit at signed rates. Every floor remainder is recorded as an exact fraction and charged to nobody. Two conservation identities hold exactly:

- per currency, $\sum_{\text{participants}} \mathrm{pos} + \mathrm{agent} = 0$;
- in settlement-unit terms, participant positions sum to zero, and so do agent positions.

The intraday net debit cap is interest-free and defaults to zero. The module also carries tables for the 55 African Union member states with their currencies and monetary unions (`cb_ccy.h`). Interoperability with PAPSS is **design intent**. [`docs/CENTRAL_BANK.md`](../CENTRAL_BANK.md) states: "not connected to PAPSS, not onboarded, not certified".

## 13. Status of claims

Labels: **Measured** = a number observed on a run; **Tested** = a host or CI test checks it; **Implemented, not evaluated** = code exists without an evaluation of the economic property; **Design intent** = not built. "CI" means that `make -C kernel verify-all` runs it (`.github/workflows/ci.yml`).

| Claim | Status | Evidence |
|---|---|---|
| L1 to L5 ledger invariants hold after every posting | Tested (CI, ASan/UBSan) | `kernel/src/pay/pay_ledger.h`, `kernel/src/pay/test_pay.c` |
| P1 to P5 and P7 to P11 hold on a hand-written model over small bounds | Tested (TLC, bounded) | `proofs/ledger_conservation.tla`, `proofs/ledger_replay.tla`, `proofs/README.md` |
| P6: no int64 overflow at the real widths | Tested (Lean, all values) | `proofs/rational_bounds/RationalBounds.lean` |
| Market and ledger books reconcile every cycle, and the spine halts on mismatch | Tested (CI; 89 checks passed on our run) | `kernel/src/settle/settle.h`, `kernel/src/settle/test_settle.c` |
| Fee $= \lfloor 8889g/10^7 \rfloor$, the carry is exact, the split is 50/25/15/10 with the remainder to the reserve | Tested (CI) | `kernel/src/pay/pay_assure.h`, `kernel/src/pay/test_pay.c`, `kernel/src/pay/gen_assure_ref.py` |
| Settlement spine and fee loop are linked into the hosted app | Implemented, not evaluated | `kernel/arch/hosted/zxv_host.c` |
| Solvency vault: one-third sweep, $\lfloor N/3 \rfloor$ cap, exclusion from the floor, first loss | Tested (CI; 160 checks passed on our run) | `kernel/src/svault/svault.h`, `kernel/src/svault/test_svault.c` |
| Backed-only descending uniform-price issuance with haircutted signed receipts | Tested (CI; 349 checks passed on our run) | `kernel/src/vauction/vauction.h`, `kernel/src/vauction/test_vauction.c` |
| Swap at $R/(N-h)$ never lowers the ratio | Tested (CI; 184 checks passed) | `kernel/src/vswap/vswap.h`, `kernel/src/vswap/test_vswap.c` |
| V-Bonds and V-Calls/V-Perps have no rate field, are fully collateralised, and are off by default | Tested (CI; 183 and 208 checks passed) | `kernel/src/vbond/vbond.h`, `kernel/src/vderiv/vderiv.h` |
| Zenny basket claim equals the deposit exactly | Tested (CI; 230 checks passed) | `kernel/src/zenny/zenny.h`, `kernel/src/zenny/test_zenny.c` |
| Tiers give no auction priority; founder powers need 3-of-5 stewards plus a 7-day lock | Tested (CI; 315 checks passed) | `kernel/src/sicily/sicily.h`, `kernel/src/sicily/test_sicily.c` |
| The vault, auction, swap, bond, derivative, basket and tier modules run in the application | Design intent (no caller outside tests) | `grep` of `kernel/arch`, `sdk` (Section 2) |
| No-usury guard refuses time-accruing charges | Tested (CI) | `kernel/src/cbank/cb_usury.h`, `kernel/src/cbank/test_cb_usury.c` |
| Proportional-share market, caps M4/M7, conservation M8, Crown inalienability M9 | Tested (CI) | `kernel/src/swarm/swarm_market.h`, `kernel/src/swarm/test_swarm_economy.c` |
| Truthfulness, efficiency or equilibrium properties of the swarm market or the auction | Implemented, not evaluated | none: no analysis in the repository |
| Nine-form canonical order is consistent across modules | Tested (CI) | `kernel/src/zcapital/zcap_forms.h`, `kernel/src/zcapital/test_capital_canon.c` |
| `pay` and cbank ISO 20022 output validates against base XSDs | Tested (CI) | `kernel/src/pay/pay_iso.h`, `kernel/Makefile` (`xmllint`), `kernel/src/pay/xsd/PROVENANCE.txt` |
| PAPSS-style netting conserves exactly per currency and per settlement unit | Tested (CI) | `kernel/src/cbank/cb_net.h`, `kernel/src/cbank/test_cb_net.c` |
| PAPSS or African Union interoperability | Design intent | `docs/CENTRAL_BANK.md` "Honest limits" |
| Open batch-auction secondary market | Design intent | `docs/ARCHITECTURE.md` §17 |
| Demurrage on VFV or vouchers | Design intent, undecided ("Not built") | `docs/ARCHITECTURE.md` §18 |

Check counts marked "on our run" come from running `make -C kernel test-econ test-settle` on commit `0888621` while this report was written. All targets passed.

## 14. Limitations and threats to validity

1. **Stale status documents.** [`docs/GAP_AUDIT.md`](../GAP_AUDIT.md) items C2 to C5 and [`docs/supplemental/CLAIMS_AND_EVIDENCE.md`](../supplemental/CLAIMS_AND_EVIDENCE.md) §4 still call V-Bill issuance, the vault and custodian receipts "not in code" or "design intent". The modules and their CI tests now exist (commits `d896e66`, `c684664` and earlier). The comment in `pay_assure.h` B1 that calls V-Bills "future work" is also out of date. The correct status is "tested, not integrated".
2. **No integration.** The modules are tested in isolation against `pay_ledger`. Their reserve assets differ (Section 5), and nothing yet routes the fee's reserve share into a vault in the running application.
3. **No mechanism analysis.** Section 15 notes that uniform-price multi-unit auctions admit demand reduction and low-price equilibria, and that the proportional-share mechanism has a known efficiency loss under strategic bidding. No equilibrium, revenue or welfare result has been derived for these variants, which have price-dependent supply, caps and pro-rata rationing.
4. **Exogenous prices.** Vault buys use a caller-supplied price. Collateral values rest on oracles and custodians, and the code can only check signatures.
5. **Model-to-code gap.** The TLA+ and Lean models are hand-written specifications, checked over small bounds (TLA+) or for all values of the model (Lean). No refinement proof links them to the C source or the binary, as seL4 has (Klein et al. 2009), so they are evidence about the design, not a verification of the ledger code. A change to the C can silently invalidate them.
6. **Fixed capacities and no persistence.** Examples: 32 bids per epoch, 64 netting participants, 512 payments per engine lifetime.
7. **The no-usury guard** classifies descriptions, not economic substance.

## 15. Relation to prior work

- **Double entry and ledgers.** The rails implement Pacioli's double entry with a derived equity column. The repository's "triple ledger" (financial, provenance and externality axes) is unrelated to Ijiri's (1986) triple-entry momentum accounting despite the name.
- **Auctions.** The issuance mechanism is a uniform-price multi-unit auction run as a descending clock with a price-dependent supply schedule. Vickrey (1961) and Milgrom (2004) give the general theory. Wilson (1979) and Back and Zender (1993) show that uniform-price share auctions can support low-price equilibria. Ausubel et al. (2014) analyse demand reduction. With endogenous or tilted supply, results change (Back and Zender, 2001; LiCalzi and Pavan, 2005). Here supply is tilted by a solvency constraint, not chosen for revenue, and its equilibrium effect is open. The swarm market is the proportional-share mechanism of Spawn (Waldspurger et al., 1992) and Kelly (1997). Johari and Tsitsiklis (2004) bound its efficiency loss with price-anticipating users at 25%, a bound that does not directly cover the capped variant here. Budish, Cramton and Shim (2015) motivate frequent batch auctions, the candidate design for the optional secondary market.
- **Banking stability.** Diamond and Dybvig (1983) locate run equilibria in demand deposits redeemable at par under sequential service. V-Bills redeem only at the current ratio $R/(N-h)$, never at par (vswap W4). This removes the first-come-first-served payoff that drives those equilibria, at the cost of the liquidity insurance that deposits provide. The code has no lender of last resort in Bagehot's (1873) sense. Lending "at a high rate" is incompatible with the no-usury rule. Liquidity in `cb_net` comes from prefunding and an interest-free net debit cap. The full-reserve flavour of the floor recalls Fisher's (1935) 100% money proposal.
- **Central bank money.** The cbank module is a prefunded multilateral net settlement model, not a CBDC. VFV is not central bank money. BIS work on CBDC (Auer and Böhme, 2020; Bank for International Settlements et al., 2020) and the CPMI-IOSCO (2012) principles are the right benchmarks for any real deployment, and none has been assessed. Kahn and Roberds (2009) survey payments economics.
- **Demurrage.** Gesell (1916), Fisher (1933) and Keynes (1936, ch. 23) discuss carrying costs on money. They are cited for context only. Demurrage on VFV is undecided and not built.
- **Forms of capital.** Multi-capital accounting has precedents in Bourdieu (1986) and in the six capitals of the IIRC (2013) framework. The nine-form scheme and its inalienable subset are this project's own construction.

## 16. References

- Auer, R. and Böhme, R. (2020). The technology of retail central bank digital currency. *BIS Quarterly Review*, March 2020.
- Ausubel, L. M., Cramton, P., Pycia, M., Rostek, M. and Weretka, M. (2014). Demand reduction and inefficiency in multi-unit auctions. *Review of Economic Studies*, 81(4).
- Back, K. and Zender, J. F. (1993). Auctions of divisible goods: on the rationale for the Treasury experiment. *Review of Financial Studies*, 6(4).
- Back, K. and Zender, J. F. (2001). Auctions of divisible goods with endogenous supply. *Economics Letters*, 73(1).
- Bagehot, W. (1873). *Lombard Street: A Description of the Money Market*. Henry S. King, London.
- Bank for International Settlements and seven central banks (2020). *Central bank digital currencies: foundational principles and core features*. Report no. 1.
- Bourdieu, P. (1986). The forms of capital. In J. Richardson (ed.), *Handbook of Theory and Research for the Sociology of Education*. Greenwood.
- Budish, E., Cramton, P. and Shim, J. (2015). The high-frequency trading arms race: frequent batch auctions as a market design response. *Quarterly Journal of Economics*, 130(4).
- CPMI-IOSCO (2012). *Principles for Financial Market Infrastructures*. Bank for International Settlements.
- Diamond, D. W. and Dybvig, P. H. (1983). Bank runs, deposit insurance, and liquidity. *Journal of Political Economy*, 91(3).
- Fisher, I. (1933). *Stamp Scrip*. Adelphi, New York.
- Fisher, I. (1935). *100% Money*. Adelphi, New York.
- Gesell, S. (1916). *Die natürliche Wirtschaftsordnung durch Freiland und Freigeld* (The Natural Economic Order).
- IIRC (2013). *The International <IR> Framework*. International Integrated Reporting Council.
- Ijiri, Y. (1986). A framework for triple-entry bookkeeping. *The Accounting Review*, 61(4).
- Johari, R. and Tsitsiklis, J. N. (2004). Efficiency loss in a network resource allocation game. *Mathematics of Operations Research*, 29(3).
- Kahn, C. M. and Roberds, W. (2009). Why pay? An introduction to payments economics. *Journal of Financial Intermediation*, 18(1).
- Kelly, F. (1997). Charging and rate control for elastic traffic. *European Transactions on Telecommunications*, 8(1).
- Keynes, J. M. (1936). *The General Theory of Employment, Interest and Money*. Macmillan.
- Klein, G., et al. (2009). seL4: Formal verification of an OS kernel. *Proc. SOSP 2009*, ACM, 207–220.
- Lamport, L. (2002). *Specifying Systems: The TLA+ Language and Tools for Hardware and Software Engineers*. Addison-Wesley.
- LiCalzi, M. and Pavan, A. (2005). Tilting the supply schedule to enhance competition in uniform-price auctions. *European Economic Review*, 49(1).
- Milgrom, P. (2004). *Putting Auction Theory to Work*. Cambridge University Press.
- Pacioli, L. (1494). *Summa de arithmetica, geometria, proportioni et proportionalità*. Venice.
- Vickrey, W. (1961). Counterspeculation, auctions, and competitive sealed tenders. *Journal of Finance*, 16(1).
- Waldspurger, C. A., Hogg, T., Huberman, B. A., Kephart, J. O. and Stornetta, W. S. (1992). Spawn: a distributed computational economy. *IEEE Transactions on Software Engineering*, 18(2).
- Wilson, R. (1979). Auctions of shares. *Quarterly Journal of Economics*, 93(4).

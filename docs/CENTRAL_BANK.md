<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->
# Central bank operations and configuration guide

This guide is for a central bank team looking at the payment module in
`kernel/src/cbank`. It covers what the module does, how to set up each
profile, how a settlement cycle runs, and what the module does not include.

## Honest limits (read first)

- **No PAPSS membership or connection.** The engine follows the same broad
  design as the Pan-African Payment and Settlement System: prefunded
  participants, local-currency legs and multilateral net settlement per
  central bank. It is not connected to PAPSS, not onboarded, not certified by
  Afreximbank or anyone else, and makes no claim to be.
- **No RTGS, SWIFT, CIPS or other network link.** The module builds ISO 20022
  messages and settlement instructions in memory. Nothing is sent anywhere.
  Connecting to a national RTGS or a messaging network is integration work
  that is not included here.
- **Schema validity is not certification.** The test suite checks the messages
  against the base ISO 20022 XSDs. It does not check them against SWIFT CBPR+,
  PAPSS or any national usage guideline. Those guidelines add rules this code
  does not know.
- **No licence and no legal review.** Running a payment system needs legal
  authority, an oversight framework, settlement-finality law and operating
  rules. None of these comes with this code.
- **No sanctions list.** Sanctions and AML screening is a hook (an interface
  your own screening system plugs into). No list, model or provider is
  included. If screening is required and no hook is installed, every payment
  is refused.
- **Reference data has an as-of date.** The ISO 4217 table was generated from
  SIX "List One" dated 2026-01-01 (see `kernel/src/cbank/data/PROVENANCE.txt`).
  The African Union member list was written from general knowledge as of
  2026-10-09. It does not record which members are currently suspended from
  AU activities.
- **The VINO Sovereign Standard (VSS) is a proposal.** It is the author's
  voluntary request-for-comment, not an adopted standard. A Bronze, Silver or
  Gold result from these checks is a self-test, not an audit or a
  certification.
- **No interest, anywhere.** The module has no interest-bearing instrument,
  no overdraft rate and no late-payment accrual. Intraday liquidity comes from
  prefunding plus an optional interest-free net debit cap.

## What the module does

| Part | File | What it gives you |
|---|---|---|
| Currency reference | `cb_ccy.h` | Every active ISO 4217 code (178), every ISO 3166-1 country (249), which currencies each country uses, and the 55 AU member states with AU region and monetary union. You can look things up by alpha code, numeric code or country. |
| Configuration | `cb_config.h` | Operator role, currency profiles, settlement calendars, prefunding and reserve rules, capital-flow switches, corridor limits, FX rate sources with signed rate records, the screening hook and a fee schedule that is checked for usury. |
| Payment engine | `cb_net.h` | Accepts payments in each participant's local currency, converts them at signed rates, keeps net positions, enforces limits, handles returns and cancellations, and closes the cycle with one net settlement instruction for each central bank. |
| Messages | `cb_mx.h` | Bounded ISO 20022 writers: pacs.008, pacs.009, pacs.002, pacs.004, camt.056 and camt.029, plus the VSSReceipt block. |
| Conformance | `cb_vss.h` | VSS checks C1 to C4, a C5 hook and the Bronze/Silver/Gold levels. It also checks the 811 resolution procedure record. |
| No-usury guard | `cb_usury.h` | Refuses any fee or instrument whose charge grows with elapsed time. |

Every amount is a whole number of ISO 4217 minor units. The code has no
floating point, no rounding you cannot see, no dynamic memory and no libc.

## Roles

Call `cb_config_init(&cfg, role, name, bic, country)` to set the role.
Each role gets these defaults from `cb_role_default`:

| Role | Default permissions | VFV | Notes |
|---|---|---|---|
| Central bank | operate cycles, admit participants, pacs.009, hold prefund, return, cancel | **off** | No currency by default. Add the currencies you issue or settle. |
| Commercial bank participant | pacs.008, pacs.009, hold prefund, return, cancel | off | A direct participant in the engine. |
| Self-banking operator | pacs.008, hold prefund, return, cancel, VFV | on | The platform's own node. It cannot send pacs.009. |
| Institution | pacs.008, cancel; needs a sponsor | off | Appears as debtor or creditor behind a sponsoring bank. |
| Individual | pacs.008, cancel; needs a sponsor | off | Same. A person can never be a direct participant. |

Screening is required for every role by default.

**About VFV.** The Vino Floating Voucher is the platform's own unit. Its
rails are DEBIT 846, CREDIT 810 and EQUITY 888. A central-bank profile never
defaults to VFV. The configuration refuses "VFV" and the rail numbers as
currency profiles: settlement always happens in real ISO 4217 currencies.
Rail numbers appear only inside the VSSReceipt in `SplmtryData`.

## Configuring a currency

`cb_config_add_ccy(&cfg, "XOF", "BCEAO", "<BIC>", "SN", true)` adds a profile.
The code takes the minor units from ISO 4217 (XOF has 0, KWD has 3, and so
on). You cannot override them.

| Setting | Field | Default | Meaning |
|---|---|---|---|
| Issuing authority | `issuer_name`, `issuer_bic`, `issuer_country` | required | The central bank that receives this currency's net settlement instruction. |
| Legal tender | `legal_tender` | caller | If true, list one must show the currency in use in the issuer's country. For example, XOF with a Nigerian issuer is refused. |
| Rounding | `rounding` | `CB_ROUND_EXACT` | EXACT refuses any conversion that leaves a remainder. FLOOR_REPORTED rounds down and records the remainder on the payment as an exact fraction. Nobody is ever charged for it. |
| Window | `cb_config_set_window` | 08:00 open, 16:00 cut-off, 17:00 close, UTC+0, Saturday and Sunday closed | Times are local minutes past midnight. The UTC offset is in minutes. `CB_WEEKEND_FRI_SAT` is available. |
| Holidays | `cb_config_add_holiday(y, m, d)` | none | Up to 64 dates per currency. |
| Prefunding | `prefund_required`, `min_prefund` | required, 0 | A participant below the minimum is admitted inactive. |
| Reserve | `reserve_bps` | 0 | Share of the prefund that cannot be used for payments, from 0 to 10000 basis points. |
| Net debit cap | `net_debit_cap` | 0 | Interest-free intraday credit beyond the prefund. 0 means fully prefunded. |
| Capital-flow switches | `cfm` | outbound, inbound and non-resident on | `CB_CFM_OUTBOUND`, `CB_CFM_INBOUND`, `CB_CFM_PURPOSE_REQ` (an ISO purpose code is required), `CB_CFM_NONRESIDENT` (participants outside the issuer country may hold the currency) and `CB_CFM_HALT` (emergency stop). |
| Outbound cap | `cfm_max_outbound` | 0 (none) | Largest cross-currency payment out of this currency. |

**Corridors.** `cb_config_add_corridor(&cfg, "XOF", "NGN", per_payment,
per_cycle)` sets limits in the paying currency. With
`corridor_default_deny` (the default), a currency pair without a corridor is
refused.

**Settlement unit.** `cfg.settle_unit` is the ISO 4217 code used to value
positions across currencies, for example USD, EUR or ZAR. You choose it. It
must also be added as a currency profile. It is only a unit of account inside
the engine.

**FX rate sources.** `cb_config_add_source(&cfg, id, name, ed25519_pubkey,
max_age_seconds)` adds a source. Each rate record says "1 major unit of the
currency = mant / 10^scale major units of the settlement unit". It carries a
publication time, a valid-until time and a sequence number, and it is signed
with Ed25519 over a fixed 48-byte canonical encoding (`cb_rate_canon`). Set
`cfg.verify` to a signature verifier. The tests use
`kernel/src/robin_debanks/ed25519_verify.c`. A record is refused if the
signature is bad, the source is unknown or disabled, it is older than the
source's maximum age, it has expired, or it replays an older sequence number.
At payment time, a rate older than the maximum age counts as missing, and the
payment is rejected with MS03.

**Screening hook.** Set `cfg.screen` to a function. It receives the message
id, both agents' BICs, debtor and creditor names and countries, the currency,
the amount and the purpose code. It returns CLEAR, HIT, REVIEW or ERROR.
HIT and REVIEW reject the payment with RR04. The engine has no hold queue, so
REVIEW also rejects. ERROR rejects with MS03 (fail closed).

**Fee schedule.** `cfg.fees` is checked by `cb_usury_check_schedule` during
`cb_config_validate`. A schedule with any time-based charge fails with
`CB_E_USURY`. The engine does not charge fees itself.

Call `cb_config_validate(&cfg)` before starting. It returns the first problem
it finds: no currency, a bad code, an invalid calendar, a missing settlement
unit, sources without a verifier, screening required without a hook, and so
on.

## How a settlement cycle works

1. **Admit participants.** `cb_net_add_participant(&eng, bic, country, ccy,
   role, prefund)`. Each participant holds one currency, prefunded at that
   currency's central bank.
2. **Load rates.** `cb_net_rate_submit` for every currency except the
   settlement unit.
3. **Open the cycle.** `cb_net_open_cycle`.
4. **Payments.** `cb_net_submit` takes a payment from participant S to
   participant R, with the amount in S's currency. The engine checks these in
   order: cycle open, both participants known and active, the sender's role
   permission, the amount, the country codes, the capital-flow switches, both
   currencies' business windows, the corridor limits, screening, conversion
   and rounding rule, and liquidity. If all pass, the payment is ACCEPTED
   (pacs.002 ACSP). Otherwise it is REJECTED with an ISO reason code:

   | Code | Cause |
   |---|---|
   | TM01 | Outside the window or after cut-off, holiday, weekend, or no open cycle |
   | RC01 / AC06 | Unknown or inactive participant |
   | AG01 | Not permitted for this role, or the corridor is closed |
   | AM01 / AM02 / AM14 | Zero amount; over the per-payment limit; over the per-cycle corridor limit |
   | RR04 | Capital-flow switch, missing purpose code, halt, or screening hit |
   | MS03 | No fresh signed rate, or the screening system failed |
   | AM12 | The conversion is not exact under the EXACT rule |
   | AM04 | Not enough prefund (after reserve and debit cap) |
   | DUPL | A message id reused with different content |
   | BE09 | Invalid country code |

   The codes come from the ISO 20022 external code sets as known when this
   was written. Check them against the current release before production.
5. **Conversion.** The engine values the payment in the settlement unit U,
   then converts it to R's currency, rounding down each time:
   `u = floor(send x m_S x 10^e_U / 10^(s_S+e_S))` and
   `recv = floor(u x 10^(s_R+e_R) / (m_R x 10^e_U))`.
   `cb_net_quote` shows what R will receive before you send. Payments in a
   single currency need no rate.
6. **Idempotency.** Resubmitting the same message id gives the same answer
   and books nothing. The same id with different content is rejected (DUPL)
   and the original stays as it was.
7. **Returns (pacs.004).** `cb_net_return(orig, return_id, reason)` books the
   exact reverse of the original legs at the original amounts, with no new
   conversion. It works in the same cycle or a later one, once per payment.
   The original payee must have enough liquidity to fund it.
8. **Cancellations (camt.056 / camt.029).** `cb_net_cancel(orig, case_id)`
   reverses a payment that was accepted in the current open cycle. The answer
   is CNCL with ACCR. Otherwise it is RJCR with a reason: ARDT (already
   returned or cancelled), AGNT (already settled; use a return), ARJR (it was
   rejected), NOOR (unknown) or AM04 (the payee cannot fund the reversal).
   Case ids are idempotent.
9. **Close the cycle.** `cb_net_close_cycle` checks conservation and then
   writes one `cb_settle_instr` per configured currency. Each instruction
   names the issuing central bank and lists every participant's net in that
   currency, the total, and the total against the settlement agent. Each
   participant's net is applied to its prefund. Accepted payments become
   SETTLED (pacs.002 ACSC). A pacs.009 can then carry each currency's net to
   or from the settlement agent's account.

### Conservation (what is guaranteed)

For one payment, the engine books S −send and agent(ccy S) +send, then
R +recv and agent(ccy R) −recv. Two invariants hold exactly, with no
tolerance:

1. For every currency, the participants' positions plus the agent's
   position add up to zero.
2. In settlement-unit terms, the participants' unit positions add up to
   zero, and so do the agent's unit positions per currency.

Each instruction's lines add up to its total, the agent's net equals the
negative of that total, and the unit values of all instructions add up to
zero. The tests check this after every step of a 300-payment random run.
What the invariants do not cover is the agent's FX liquidity across currency
pools. The agent ends a cycle long in some currencies and short in others,
and funding those positions is an operational arrangement outside this code.

## ISO 20022 messages

| Message | Use |
|---|---|
| pacs.008.001.08 | Customer credit transfer from a participant |
| pacs.009.001.08 | Bank's own transfer; net settlement legs after cycle close |
| pacs.002.001.10 | Status: ACSP accepted, ACSC settled, RJCT with reason |
| pacs.004.001.09 | Return of an accepted or settled payment |
| camt.056.001.08 | Cancellation request |
| camt.029.001.09 | Resolution: CNCL/ACCR, or RJCR with a reason |

Rules every writer enforces:

- `Ccy` is always an ISO 4217 alphabetic code, never VFV. Amounts are
  written with the ISO minor units.
- Country codes are validated ISO 3166-1 codes of real parties. VFV never
  gets a country, so no "NC" or "NCR".
- BICs and UETRs are checked against the ISO patterns.
- Output never runs past the caller's buffer.

The VSSReceipt goes in `SplmtryData/Envlp` under the namespace
`urn:zedec:vss:receipt:1`. Systems that do not know VSS skip it. Its
elements, in order, are RcptId, SysRef, RailDr, RailCr, Amt, EqtyCoord,
BckgDscl (EFCT or ASPL, ASPL by default), XwalkRef, AnchrRef and Sgntr. The
rails are 846, 810 or 888. The block is validated against
`kernel/src/cbank/xsd/vss.receipt.1.xsd`, and it is not registered with the
ISO 20022 Registration Authority.

## VSS conformance

| Check | What passes |
|---|---|
| C1 Balance | In each transaction, the debit legs equal the credit legs exactly, in minor units. |
| C2 Receipt | Exactly one complete, signed receipt per transaction, with matching amount, currency and rails. The receipt log is append-only. |
| C3 Reversibility | Trial-balance lines map into VSS records through the published crosswalk and back out unchanged. The crosswalks cover US GAAP, IFRS (ECL contra, OCI on the equity rail), IPSAS (a fund tag is required) and AAOIFI (musharakah, mudarabah, URIA and profit distribution on the equity rail 888). Interest lines are refused in every framework. |
| C4 Disclosure | Effective and aspirational backing are summed apart. Effective backing needs an attestation reference. A presented figure that counts aspirational backing as effective fails. |
| C5 Integrity | Through hooks: each identifier passes its own rail's check digit and fails the other two. Connect `cn_luhn_valid` (846), `cn_damm_valid` (810) and `cn_verhoeff_valid` (888) from `kernel/src/cardnet/cn_check.h`. With no hooks, C5 does not run. |

The levels are Bronze (C1, C2, C5), Silver (C1 to C3, C5) and Gold (C1 to
C5, plus an independent attestation of C4). A failed C4 removes every level,
and a level claim must state the date of its run.

The test suite found one problem in the proposal itself. Its Appendix D
Verhoeff example, 8840000000000007, also passes Luhn, so it breaks the
cross-rail property the proposal claims for it. 8840000000000011 is a working
replacement.

**811 resolution class.** This record handles orphaned obligations, as
described in Part V of the proposal. It is a procedure record, not a rail.
The check requires designator 811, a basis (dissolved entity, statute-barred,
irreconcilable legacy or defunct state), references to the authority, the
notice and the adjudication, and a valid audit receipt that points to the
claim. A claim against a living natural person is always refused: 811 is
never a way to cancel a living debtor's enforceable debt.

## The no-usury guard

Any module can call `cb_usury_check_fee`, `cb_usury_check_schedule` or
`cb_usury_check_instrument`.

**Refused:** interest at any rate, coupons, zero-coupon accretion, negative
rates, per-period accrual, compounding, late charges that grow with time,
rollover fees, percentages of an outstanding balance, and "profit sharing"
that guarantees a return or is not booked on the equity rail.

**Allowed:** flat fees per event, fixed ad valorem fees per event, a one-off
penalty that does not grow and goes to charity or cost recovery, and real
profit-and-loss sharing on rail 888.

The guard judges the description it is given. A real product still needs
legal and Shari'ah review.

## What is NOT included

- PAPSS onboarding, membership, testing or certification
- Links to any RTGS, central bank accounting system or core banking system
- SWIFT, CIPS, SIC, TARGET or other network connectivity; no BAH (head.001)
  wrapping, no transport and no PKI
- SWIFT CBPR+ or market-infrastructure usage-guideline validation
- A sanctions, PEP or AML list or model
- Persistent storage, high availability, disaster recovery or audit-log
  storage. The engine is in memory, with a fixed capacity of 64 participants
  and 512 payment records.
- Partial returns, payments where the receiver's amount is fixed (quote
  first, then send), queueing or gridlock resolution
- Licensing, an oversight assessment against the CPMI-IOSCO Principles for
  Financial Market Infrastructures, settlement-finality law, operating rules,
  participant agreements and legal review
- Market FX rates. The test rates are illustrative round numbers, not market
  data.

## Verifying it yourself

From `kernel/`, the `verify-all` target builds and runs five host test
programs. It also validates the generated XML against the base ISO 20022
XSDs and the VSSReceipt XSD, cross-compiles every module file for
freestanding AArch64, and checks that no libc symbol is used. To regenerate
the reference tables after a new SIX list is published:

```
python3 -I kernel/src/cbank/gen_cb_tables.py kernel/src/cbank/data/list-one.xml \
    kernel/src/cbank/data/iso_3166-1.json > kernel/src/cbank/cb_ccy_tbl.c
```

## African Union member states and their currencies

This table is generated from the code tables. It shows the ISO 4217 data as
of SIX list one 2026-01-01 and AU membership as of 2026-10-09. Unions:
WAEMU (BCEAO, XOF), CEMAC (BEAC, XAF), CMA (Common Monetary Area, ZAR
anchor). For the Sahrawi Arab Democratic Republic, ISO 3166 has only the
territory code EH/ESH ("Western Sahara"), and ISO 4217 lists MAD for that
entry.

| Member state | ISO 3166 a2 / a3 / num | AU region | Currency: ISO 4217 alpha / num / minor units | Union |
|---|---|---|---|---|
| Algeria | DZ / DZA / 012 | North | DZD / 012 / 2 (Algerian Dinar) |  |
| Angola | AO / AGO / 024 | Southern | AOA / 973 / 2 (Kwanza) |  |
| Benin | BJ / BEN / 204 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Botswana | BW / BWA / 072 | Southern | BWP / 072 / 2 (Pula) |  |
| Burkina Faso | BF / BFA / 854 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Burundi | BI / BDI / 108 | Central | BIF / 108 / 0 (Burundi Franc) |  |
| Cabo Verde | CV / CPV / 132 | West | CVE / 132 / 2 (Cabo Verde Escudo) |  |
| Cameroon | CM / CMR / 120 | Central | XAF / 950 / 0 (CFA Franc BEAC) | CEMAC |
| Central African Republic | CF / CAF / 140 | Central | XAF / 950 / 0 (CFA Franc BEAC) | CEMAC |
| Chad | TD / TCD / 148 | Central | XAF / 950 / 0 (CFA Franc BEAC) | CEMAC |
| Comoros | KM / COM / 174 | East | KMF / 174 / 0 (Comorian Franc) |  |
| Congo | CG / COG / 178 | Central | XAF / 950 / 0 (CFA Franc BEAC) | CEMAC |
| Cote d'Ivoire | CI / CIV / 384 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Democratic Republic of the Congo | CD / COD / 180 | Central | CDF / 976 / 2 (Congolese Franc) |  |
| Djibouti | DJ / DJI / 262 | East | DJF / 262 / 0 (Djibouti Franc) |  |
| Egypt | EG / EGY / 818 | North | EGP / 818 / 2 (Egyptian Pound) |  |
| Equatorial Guinea | GQ / GNQ / 226 | Central | XAF / 950 / 0 (CFA Franc BEAC) | CEMAC |
| Eritrea | ER / ERI / 232 | East | ERN / 232 / 2 (Nakfa) |  |
| Eswatini | SZ / SWZ / 748 | Southern | SZL / 748 / 2 (Lilangeni) | CMA |
| Ethiopia | ET / ETH / 231 | East | ETB / 230 / 2 (Ethiopian Birr) |  |
| Gabon | GA / GAB / 266 | Central | XAF / 950 / 0 (CFA Franc BEAC) | CEMAC |
| Gambia | GM / GMB / 270 | West | GMD / 270 / 2 (Dalasi) |  |
| Ghana | GH / GHA / 288 | West | GHS / 936 / 2 (Ghana Cedi) |  |
| Guinea | GN / GIN / 324 | West | GNF / 324 / 0 (Guinean Franc) |  |
| Guinea-Bissau | GW / GNB / 624 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Kenya | KE / KEN / 404 | East | KES / 404 / 2 (Kenyan Shilling) |  |
| Lesotho | LS / LSO / 426 | Southern | LSL / 426 / 2 (Loti); ZAR / 710 / 2 (Rand) | CMA |
| Liberia | LR / LBR / 430 | West | LRD / 430 / 2 (Liberian Dollar) |  |
| Libya | LY / LBY / 434 | North | LYD / 434 / 3 (Libyan Dinar) |  |
| Madagascar | MG / MDG / 450 | East | MGA / 969 / 2 (Malagasy Ariary) |  |
| Malawi | MW / MWI / 454 | Southern | MWK / 454 / 2 (Malawi Kwacha) |  |
| Mali | ML / MLI / 466 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Mauritania | MR / MRT / 478 | North | MRU / 929 / 2 (Ouguiya) |  |
| Mauritius | MU / MUS / 480 | East | MUR / 480 / 2 (Mauritius Rupee) |  |
| Morocco | MA / MAR / 504 | North | MAD / 504 / 2 (Moroccan Dirham) |  |
| Mozambique | MZ / MOZ / 508 | Southern | MZN / 943 / 2 (Mozambique Metical) |  |
| Namibia | NA / NAM / 516 | Southern | NAD / 516 / 2 (Namibia Dollar); ZAR / 710 / 2 (Rand) | CMA |
| Niger | NE / NER / 562 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Nigeria | NG / NGA / 566 | West | NGN / 566 / 2 (Naira) |  |
| Rwanda | RW / RWA / 646 | East | RWF / 646 / 0 (Rwanda Franc) |  |
| Sahrawi Arab Democratic Republic | EH / ESH / 732 | North | MAD / 504 / 2 (Moroccan Dirham) |  |
| Sao Tome and Principe | ST / STP / 678 | Central | STN / 930 / 2 (Dobra) |  |
| Senegal | SN / SEN / 686 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Seychelles | SC / SYC / 690 | East | SCR / 690 / 2 (Seychelles Rupee) |  |
| Sierra Leone | SL / SLE / 694 | West | SLE / 925 / 2 (Leone) |  |
| Somalia | SO / SOM / 706 | East | SOS / 706 / 2 (Somali Shilling) |  |
| South Africa | ZA / ZAF / 710 | Southern | ZAR / 710 / 2 (Rand) | CMA |
| South Sudan | SS / SSD / 728 | East | SSP / 728 / 2 (South Sudanese Pound) |  |
| Sudan | SD / SDN / 729 | East | SDG / 938 / 2 (Sudanese Pound) |  |
| Tanzania | TZ / TZA / 834 | East | TZS / 834 / 2 (Tanzanian Shilling) |  |
| Togo | TG / TGO / 768 | West | XOF / 952 / 0 (CFA Franc BCEAO) | WAEMU |
| Tunisia | TN / TUN / 788 | North | TND / 788 / 3 (Tunisian Dinar) |  |
| Uganda | UG / UGA / 800 | East | UGX / 800 / 0 (Uganda Shilling) |  |
| Zambia | ZM / ZMB / 894 | Southern | ZMW / 967 / 2 (Zambian Kwacha) |  |
| Zimbabwe | ZW / ZWE / 716 | Southern | ZWG / 924 / 2 (Zimbabwe Gold) |  |

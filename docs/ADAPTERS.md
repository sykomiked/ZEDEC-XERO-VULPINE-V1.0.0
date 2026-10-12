# Adapters: accounting, jurisdiction plug-ins, AI intent, privacy

Four kernel modules that sit between ZXV's ledger and the outside world's
rules. Each is freestanding C11 (no allocation, no floating point) with its own
test in `kernel/Makefile` (`make test-adapters`, part of `verify-all`) and a
mutation target in `tests/harness/mutator/targets.json`.

The wording matters: every adapter is **designed to follow** a framework or
**designed to support** a regime. None is certified by, or affiliated with, the
IFRS Foundation, the FASB, AAOIFI or any regulator, and none replaces an
accountant, an auditor, a tax adviser, a lawyer or a Shariah board.

## Accounting adapters (`kernel/src/acct`)

| Call | What it does |
|---|---|
| `acct_translate_transaction` | One economic event (a sale, a Vino voucher, a V-Bill issue, a murabaha, interest, tax, zakah and 20 more) into one balanced journal entry in the framework's captions, with the paragraph it follows |
| `acct_post` / `acct_record` | The entry into a book; all or nothing, overflow-checked |
| `acct_generate_balance_sheet` | The book as a statement of financial position in the framework's layout, with a JSON form for zxvd |
| `acct_reconcile_tax_schedule` | Tax at the statutory rate to tax expense, item by item (IAS 12.81(c), the ASC 740 rate reconciliation); under AAOIFI also the zakah base and zakah |

Frameworks built: **IFRS**, **US GAAP**, **AAOIFI**. IPSAS, OHADA, CAS and
insurer SAP (the other names zxvd accepts) return "not supported" rather than a
guess. Where AAOIFI has no standard of its own, the AAOIFI adapter uses the
IFRS treatment.

Where the frameworks differ, the adapters differ:

| Event | IFRS | US GAAP | AAOIFI |
|---|---|---|---|
| LIFO cost of sales | Refused (IAS 2.25) | Allowed (ASC 330) | Refused |
| Development costs meeting the criteria | Intangible asset (IAS 38.57) | Expense (ASC 730-10-25-1) | Intangible asset |
| Upward revaluation of PPE | Revaluation surplus (IAS 16.39) | Refused | Revaluation surplus |
| V-Bills the holder can redeem | Financial liability (IAS 32.18(b)), or equity under the puttable exception | Temporary equity (ASC 480-10-S99) | Equity of investment account holders |
| Murabaha receivable | Amortised cost | Amortised cost | Gross, less deferred profit (FAS 28) |
| Interest received | Finance income | Interest income | **Refused by default**; when flagging is chosen, set aside for charity, never income |

**Riba.** Under AAOIFI any interest event is refused unless the deployment
chooses to flag instead. A flagged entry carries a marker for the Shariah
supervisory board. The adapter only flags: whether an arrangement is Shariah
compliant is a ruling for a board.

### Tested against published worked examples

| Example | Source | What the test checks |
|---|---|---|
| IAS 12 Example 2, 81(c)(i) and (ii) | IFRS Foundation, IFRS Accounting Taxonomy 2024 illustrative example 10 | 8,775 at 40% = 3,510; tax expense 3,990; 8,740 at 35% with the rate change = 2,054; 45.5% and 23.5% |
| IAS 1 IG6, XYZ Group | IFRS Accounting Taxonomy 2024 illustrative example 2 | Every subtotal, both years: total assets 1,466,500 and 1,524,200 |
| ASC 740 rate reconciliation | RSM, ASU 2023-09 guide, Example 7-2; BDO Example 2-4 | $2,205,000 at 21%, total $1,395,500 and 13.3%; the $34,860 threshold |
| ASC 606 Example 52 (loyalty points) | FASB ASU 2014-09 | 91,324 / 8,676; 4,110 then 3,493; liabilities 4,566 and 1,073 |
| Gift-card breakage | Journal of Accountancy, February 2015 | $18 of breakage on a $162 redemption |
| Murabaha (FAS 28) | Moosa, J. Risk Financial Manag. 16 (2023) 335 | Every year's profit, deferred profit and net receivable |
| Zakah rate | Emirates NBD shareholder zakat note, 2024 | 2.5775% for a Gregorian year: AED 0.5786 a share |
| Zakah base | El-Badawi and Al-Sultan (1992) | Net working capital 128,500 |
| LIFO | Cleverence worked example | Cost of sales $3,290, ending inventory $1,330; refused under IFRS |

Two published figures are wrong in their sources, and the tests say so: the
murabaha paper prints year 2 deferred profit as 2,080.52 (it is 2,080.54), and
RSM prints the valuation-allowance row as (6.6)% (700,000 / 10,500,000 is
(6.7)%). No published numeric AAOIFI FAS 1 statement or FAS 39 zakah
computation with a rate applied could be fetched; those parts are tested
against the published structure and the adapter's own arithmetic, labelled
"ours" in `test_acct.c`.

## Jurisdiction plug-in ABI (`kernel/src/juris`)

Rules that differ by country live in plug-ins: capital controls, tax rates,
which accounting framework applies, whether the open market may run.

- **Settings come from zxvd.** `juris_settings_parse` reads the
  `jurisdiction.get` result (PR #69) strictly: market, standard, country and
  `in_effect`.
- **Plug-ins are signed.** A manifest signed by the deployment's jurisdiction
  gateway key (ML-DSA-87 in production) binds the plug-in's id, country, hooks,
  version and the SHA-256 of its code. Older versions cannot be loaded over
  newer ones.
- **Only the chosen country's plug-ins load**, and changing country unloads
  the others.
- **It fails closed.** Every answer is a 64-state tag, combined so the worst
  wins. No plug-in answering means wait. Two plug-ins giving different tax
  rates escalates to a person. A forbidden answer goes to the hash-chained
  quarantine log, and a full log halts the host.
- **The V-Bill hub's limit rule is the first plug-in**
  (`juris_limit_plugin`), and a host can stand in as a vhub rule
  (`juris_vhub_rule`).

zxvd reports `in_effect: false` until it hands its settings to this host; that
wiring follows once PR #69 is merged.

## AI intent layer (`kernel/src/intent`)

A local model can propose a payment, an invoice, a route or a report. It never
writes a balance.

- Model output must match a small grammar exactly (`pay 12.50 VFV to 42`,
  `invoice …`, `route …`, `report balance_sheet`). Anything else is refused,
  including text with extra instructions appended.
- A guard checks every payment: it is paid from the owner's account, is
  within the per-proposal cap and the epoch budget, does not go to the payer or
  a denied account, stays within the proposal rate, and passes the
  jurisdiction host.
- A draft is released only with the owner's signature over its SHA-256
  digest and sequence number, once, before it expires. A draft changed after
  approval no longer matches.
- Reports and routes are read-only and need no approval. Route options are
  ranked: allowed first, then most received, lowest fee, fewest hops.

## Privacy layer (`kernel/src/privacy`)

Designed to support GDPR- and HIPAA-style controls; not a compliance claim.

- **Exact differential privacy.** Discrete Laplace noise drawn with integer
  arithmetic only (Canonne, Kamath and Steinke 2020), so none of the
  floating-point attacks on Laplace noise apply.
- **A privacy budget.** Each release spends its epsilon as an exact fraction;
  a release past the budget is refused before any noise is drawn.
- **Four-valued access control.** Allow only gives access; a matching allow
  and deny escalates to a person; no rule never allows. Special-category data
  needs the subject's explicit, current consent for this purpose, and records
  past their retention period are not readable.
- **Selective disclosure.** Salted SHA-256 claim digests signed by an issuer,
  so a holder can show `age_over_18` without the birth date.

No zero-knowledge proofs are built here: ZK proofs need a vetted, audited
library, and this layer will wrap one rather than write its own.

## Not done here

- **zxvd wiring.** The `jurisdiction.*` methods arrive with PR #69; handing
  their settings to the jurisdiction host, and RPC methods for the balance
  sheet and the tax schedule, follow once it merges.
- **libzxv ABI.** These modules are not in libzxv yet. The SDK thread owns the
  ABI (1.5 already carries a `treaty` group for COBOL copybook treaties and
  Sutra); the accounting, jurisdiction, intent and privacy groups are listed
  for it to bind in a later minor version.

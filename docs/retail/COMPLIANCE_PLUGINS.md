# Compliance plug-ins: capital controls, tax, KYC

Roadmap section 6.6a, retail track R3: steps R3.4, R3.5 and R3.6.
Code: `kernel/src/capctl`, `kernel/src/taxhook` and `kernel/src/kycwrap`. Tests: `make -f Makefile -f mk/compliance.mk test-compliance` (run from `kernel/`).

All three plug-ins handle regulated work, so **all three are OFF by default**. Until someone turns one on, it does nothing.

These plug-ins are designed to follow published rules. None of them is legal, tax or compliance advice, and none has been reviewed by a regulator, tax adviser or lawyer. They have been tested only on a development host, not on phones or real payment rails.

---

## What the three plug-ins share: signed manifests

Each country's rules arrive as a **signed manifest**. A manifest is a small binary file with the same 68-byte header for every kind (`cp_common.h`). The header holds:

- the kind
- the country (ISO 3166-1), plus a region for sub-national tables
- a version number
- a validity window (`not_before` and `not_after`)
- the id of the key that signed it

The signature is ML-DSA-87 (FIPS 204), made with the code already in `kernel/src/pqsec`. No new cryptography was written.

The keyring (`cpk.h`) decides who may sign what. Every trusted key is limited to:

- certain kinds of manifest
- one country (or `**` for any country)

So a key trusted for Germany's tax table cannot sign France's capital-control profile.

A manifest is installed only when **all** of these hold:

1. It decodes exactly (no extra bytes).
2. Its signature is valid.
3. The current time falls inside its validity window.
4. Its version is higher than the one already installed. An older or equal version is refused, so nobody can roll back to an earlier table.

If any check fails, the old manifest stays installed.

This is the same shape as the jurisdiction plug-in ABI (R3.3: userland, signed manifest, fails closed). That ABI is being built on another branch, and the V-Bill hub's limit rule (`kernel/src/vhub`) is its first example. These plug-ins do not depend on the unmerged code. A small adapter can register them there later. For capital controls, that adapter would map:

| capctl result | Hub tag |
|---|---|
| ALLOW | PROCEED |
| DENY | false |
| Required profile missing | fail closed |

---

## R3.4 Capital-control profiles (`capctl`)

### What it does

`capctl` answers one question about a proposed cross-border transfer: **ALLOW**, **ALLOW_WITH_REPORT** or **DENY**. Every answer comes with a reason code, such as `per_tx_limit`, `corridor_blocked` or `profile_missing`.

A country's profile can set any of these rules:

- **A per-transaction limit.**
- **Rolling limits per person.** You choose the window length in days, for example 1, 30 and 365. Day boundaries are UTC.
- **Corridors.** Each rule allows or blocks a pair of countries, optionally for one currency only. A block rule always wins over an allow rule. If no rule matches, the profile's default decides.
- **Report thresholds.** A single transfer at or above the threshold, or a person's rolling total reaching it, gives ALLOW_WITH_REPORT together with a report record. A report never blocks a transfer.
- **Purpose codes.** Above a set amount, the transfer must carry a code such as an ISO 20022 purpose code (for example `SUPP`). The profile can also limit which codes are accepted.

The sending country's profile checks the sender (outbound). The receiving country's profile checks the receiver (inbound), if that profile covers inbound transfers. If either side says DENY, the transfer is denied.

All amounts are whole numbers in minor units (cents, kobo). Every addition is checked for overflow. A sum that would overflow gives DENY. It never wraps around.

### Failing closed

When the feature is on and a country is marked as **requiring** a profile (`capctl_require`), the following all give DENY for any cross-border transfer touching that country:

- no profile installed
- an expired profile
- a profile below the minimum version

A profile with a bad signature, or an older version, never gets installed in the first place, so the country stays in its previous state.

A profile that expires is treated as DENY even in a country that is not marked as requiring one. Once controls exist, they never quietly lapse.

### Turning it on

```c
capctl_t cc;
capctl_init(&cc, cpk_verify, &keyring, tally_store, n_people);  // off
capctl_require(&cc, "NG", 3);   // NG needs a profile, version 3 or later
capctl_install(&cc, manifest, len, sig, sig_len, now);
capctl_enable(&cc, true);       // on
...
capctl_evaluate(&cc, &transfer, now, &result);   // before paying
capctl_commit(&cc, &transfer, now);              // after the payment posts
```

Call `capctl_commit` only for payments that actually went through. Rolling limits count committed transfers only.

### Honest limits

- The profiles in the tests are **test data**. They do not describe any real country's rules.
- A compliance officer per country has to write and sign the real profiles.
- There is no currency conversion. The caller supplies the value in the profile's currency.
- Calendar months and years are approximated by rolling windows of N days.
- Report records are produced but not sent to any authority.
- The per-person tally store has a fixed size. When it is full, a new person is denied (`tally_full`) rather than going unchecked.

---

## R3.5 Tax-calculation hooks (`taxhook`)

**Not tax advice.** A tax adviser signs off per country before any table is used for real invoices.

### What it does

`taxhook` computes VAT, GST or sales tax from a signed rate table for each jurisdiction. It supports:

- **Categories:** standard, reduced, super-reduced, zero and exempt.
- **Effective dates:** each rate has a date range (YYYYMMDD). The rate in force on the invoice's tax-point date is used. If no rate is in force, you get an error. The hook never guesses a rate.
- **Sub-national sales tax:** a table can hold rates for parts of a country, and the rates that apply are added together. For New York City that is state 4% + city 4.5% + MCTD 0.375% = 8.875%. The caller names the regions; there is no address lookup.
- **Prices with or without tax:** with tax-exclusive prices, tax = net × rate. With tax-inclusive prices, tax = gross × rate / (1 + rate).
- **Rounding:** to the nearest minor unit, half-up or half-even, either per line or per invoice. Per-invoice rounding works per (category, rate) group. Each table sets its own convention.
- **Reverse charge for cross-border B2B sales:** when a VAT or GST table allows it and a business buys from another country, no tax is charged and the receipt says the buyer accounts for it.
- **A receipt breakdown:** per line, per (category, rate) group and in total, plus a plain-text receipt.

All arithmetic is in whole numbers, with checked ranges and no floating point.

### Sample tables

`taxhook_sample.c` ships three tables. They are clearly marked as SAMPLE DATA, as of 2026-10-12:

| Table | Rates |
|---|---|
| GB VAT | 20% / 5% / 0% |
| DE VAT | 19% / 7%, including the temporary 16% / 5% for July to December 2020 |
| US New York sales tax | 4% state, 4.5% NYC, 0.375% MCTD |

Each sample table carries a SAMPLE flag. Every receipt computed from one starts with:

> SAMPLE DATA - NOT TAX ADVICE - a tax adviser signs off per country

They were written from publicly reported rates and have **not** been checked by a tax adviser.

The tests check the arithmetic against worked examples whose results can be derived by hand. For example:

- 2.97 at 20% gives 0.59.
- 300.00 at 8.875% is exactly 26.625. Half-up gives 26.63; half-even gives 26.62.
- Ten lines of 0.02 at 20% give 0.00 tax when rounded per line, but 0.04 when rounded per invoice.

### Turning it on

```c
taxhook_t tx;
taxhook_init(&tx, cpk_verify, &keyring);   // off: compute returns TAXHOOK_E_DISABLED
taxhook_install(&tx, table, len, sig, sig_len, now);   // a table an adviser signed
taxhook_enable(&tx, true);
taxhook_compute(&tx, "DE", "", TAXHOOK_VAT, &invoice, now, &receipt);
```

### Honest limits

- No credit notes or negative amounts.
- No fractional quantities.
- No excise taxes, per-unit taxes or tax on tax.
- No mapping from products to categories.
- No filing.
- The reverse-charge rule is simplified. Real place-of-supply rules depend on what is being sold.
- Rounding conventions vary by country and sometimes by business. The table records a choice that the adviser must confirm.

---

## R3.6 Privacy-preserving KYC wrapper (`kycwrap`)

### What it does

`kycwrap` is one API for proving a fact about yourself without saying who you are. Supported facts:

- "over 18"
- "KYC'd by issuer X"
- "resident of country C"

The verifier asks with a fresh challenge. The holder answers with a proof. A pluggable backend checks the proof.

The roadmap rule is: **we do not write our own zero-knowledge cryptography**. A real zero-knowledge proof needs a vetted, audited ZK library. So there are two backends.

### 1. The ZK slot (`kyc_zk_backend`)

This defines the interface a future vetted ZK library will plug into. **Today it returns `NOT_AVAILABLE` for every request.** Nothing is faked.

### 2. The minimal-disclosure backend (`kyc_md_backend`)

This backend is **honestly not zero-knowledge**. It uses only ML-DSA-87 signatures and SHA-256. It works in five steps:

1. **Issuing.** After checking a person's documents (outside this code), the issuer issues a batch of single-use tokens. Each token carries only:
   - the predicate
   - the issuer's id and country
   - a random-looking token id
   - an expiry day
   - a fresh public key that the holder made for that token alone

   It carries no name, birth date or document number. Expiry days are whole weeks, so everyone issued in the same week shares the same expiry.
2. **Presenting.** To answer a verifier, the holder signs the verifier's challenge with that token's key. The signature also covers the verifier's id and the exact question. A proof made for one verifier therefore fails at another, and a proof made for one question fails for any other.
3. **Single use.** Each verifier keeps a set of spent tokens and refuses a token it has already seen. The holder's wallet hands each token out only once. If the spent set fills up with tokens that have not expired yet, the verifier refuses new presentations rather than accepting them unrecorded.
4. **Revocation.** The issuer publishes a signed revocation list of whole batches. By default, a verifier refuses every token from an issuer for which it has no current revocation list.
5. **Exact questions only.** An "over 18" token does not answer "over 16". Answering would reveal more than was asked, so the holder needs a token for each question.

### What this hides and what it does not

| | Hidden? |
|---|---|
| Name, birth date, address, document from the verifier | Yes |
| Two verifiers comparing notes: can they link two presentations by token contents? | No, they cannot link them. Different tokens share only fields common to everyone with the same issuer, question and expiry week |
| Issuer, question and expiry week from the verifier | **No**, the verifier sees them. For a small issuer or a rare question, that group may be small |
| Link between presentation and person, **if the issuer colludes** with a verifier | **No.** The issuer saw every token id and holder key next to the person's identity when it issued them. This is the main difference from real ZK |
| Presentations from a **revoked** batch | **No.** Revealing the batch secret makes that batch's tokens recognisable |
| Network metadata (IP addresses, timing) | Out of scope |

### Turning it on

```c
kyc_md_verifier_t md;
kyc_md_verifier_init(&md, cpk_verify, &issuer_keyring, cpk_verify_pk, spent, n);
kyc_backend_t be;  kyc_md_backend(&be, &md);   // or kyc_zk_backend(&be)
kyc_verifier_t v;  kyc_verifier_init(&v, my_verifier_id, &be);   // off
kyc_md_install_revlist(&md, list, len, sig, sig_len, now);
kyc_verifier_enable(&v, true);
kyc_request_new(&v, &pred, random32, now, &req);   // send req to the holder
kyc_verify(&v, &req, proof, proof_len, now, &result);
```

Trusted issuers go in the keyring with the kinds `CP_KIND_KYC_TOKEN` and `CP_KIND_KYC_REVOKE`, scoped to the issuer's country.

### Honest limits

- No ZK library is included, and none was invented here.
- The issuer's identity check, data retention and AML/KYC duties all sit outside this code. Those duties are where most of the legal obligations are.
- Each token is about 7.3 KB, and a presentation is about 11.9 KB, because of the ML-DSA-87 keys and signatures.
- Checking against the revocation list costs up to 64 × 256 SHA-256 operations per presentation.

---

## What has not been done yet

- **No control-socket integration.** Nothing yet connects these plug-ins to zxvd's control socket (`jurisdiction.get` and `jurisdiction.set` in `kernel/arch/hosted/zxvd_rpc.c`) or to the app settings screens. Today the plug-ins are switched on through the C calls shown above.
- **No adapter into the jurisdiction plug-in host.** That will come once R3.3 has merged.
- **No real profiles or tables.** No real capital-control profile and no adviser-approved tax table exists yet. Each one needs a qualified person per country to write and sign it.

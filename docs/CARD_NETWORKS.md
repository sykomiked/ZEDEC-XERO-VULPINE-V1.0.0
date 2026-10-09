<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Card Networks: Dragon, Phoenix and Thunderbird

An operations and configuration reference for whoever runs a card program on
`kernel/src/cardnet`. It covers what the code does, how to set it up, what each
decline means, and what it does not do.

## 1. What this is

There are three in-house card networks: Dragon, Phoenix and Thunderbird. Each
one works as a **charge card**. The holder spends during a statement cycle, and
the whole statement is due by its due day. There is no revolving balance, no
minimum payment, no interest and no late fee.

One issuer runs one network. An issuer:

- opens holder accounts,
- issues cards, each tagged with one of the nine forms of capital,
- authorizes transactions that the holder has signed with a post-quantum key
  (ML-DSA-65, FIPS 204),
- settles approved transactions onto the 555 / 777 rails,
- closes statement cycles and takes payments,
- lets holders freeze their card or replace it with a new number whenever they
  like.

The same data can go out as ISO 8583:1987 messages (`cn_iso8583.h`), inside an
EMV-style BER-TLV container (`cn_emv.h`), or through a mobile-money wallet and
USSD menus for feature phones (`cn_mobile.h`).

All code is freestanding integer C11 with fixed-size tables. It does not
allocate memory, does not call libc, uses no floating point and does no 64-bit
division.

| File | Contents |
|---|---|
| `cn_check.{h,c}` | Luhn, Damm and Verhoeff check digits |
| `cardnet.{h,c}` | networks, configuration, accounts, cards, authorization, settlement, statements, ledger, receipts |
| `cn_iso8583.{h,c}` | ISO 8583:1987 codec for 0100/0110/0200/0210/0400/0410, with VSS metadata in fields 60/61/63 |
| `cn_emv.{h,c}` | BER-TLV codec and the EMV-style authorization container |
| `cn_mobile.{h,c}` | wallets keyed by phone number, push-payment requests and callbacks, USSD menu state machine |
| `cn_util.h`, `cn_pq.h` | private helpers, and the two ML-DSA prototypes |
| `test_cardnet.c` | host tests |

## 2. The three networks

| Network | Prefix | Check algorithm | Error detection |
|---|---|---|---|
| Dragon | `880` | Luhn (mod 10) | every single-digit error; every adjacent transposition except 09 and 90 |
| Phoenix | `882` | Damm (quasigroup of order 10) | every single-digit error and every adjacent transposition |
| Thunderbird | `884` | Verhoeff (dihedral group D5) | every single-digit error and every adjacent transposition |

A card number (PAN) is 16 digits:

```
 8 8 2 | 0 4 2 | 3 6 9 9 2 0 0 2 8 | 3
prefix | issuer|  account digits   | check
```

The issuer code is the three digits set in `issuer_code`. The nine account
digits are derived (section 5). The last digit is the network's check digit.

### One number, one network (VSS invariant C5)

The VSS proposal asks that a number minted for one network fail the check of
every other network. On its own, a single decimal check digit cannot promise
that. A number that passes its own algorithm also passes each foreign algorithm
about one time in ten by chance. The test measures this: roughly 9,900 to
10,000 numbers per 100,000 for each pair of algorithms.

The proposal's own Appendix D vectors show the same thing. Taken literally:

| Base | Check | Result |
|---|---|---|
| `880000000000000` with Luhn | `8800000000000005` | fails Damm and Verhoeff: C5 holds |
| `882000000000000` with Damm | `8820000000000003` | fails Luhn and Verhoeff: C5 holds |
| `884000000000000` with Verhoeff | `8840000000000007` | **also passes Damm**: C5 fails for this raw vector |

The code therefore enforces C5 with two rules:

1. **Validation.** `cn_pan_valid(net, pan)` accepts a PAN only when all of the
   following are true: it has 16 digits, it carries that network's prefix, it
   passes that network's check, and it fails both of the other networks'
   checks. `cn_pan_network(pan)` returns the single network that accepts the
   PAN, or none.
2. **Minting.** When the issuer derives a new PAN, it skips any candidate that
   would pass a foreign check, and tries the next one.

So every PAN this code issues passes its own algorithm and fails the other two.
That is the strong form of C5 in the proposal's Part IX. No 16-digit string can
be valid for two networks, because the prefixes are distinct and validation
demands exclusivity. This holds by construction. The tests confirm it on
300,000 random numbers and on 6,000 minted PANs across the three networks.

The cost: about 19% of candidates are skipped. The space of valid numbers per
issuer shrinks by the same share, which is about 0.3 bits out of nine digits.
The exclusivity rule never weakens error detection. A mistyped number still has
to pass its own check, and now it must also fail the other two.

## 3. Charge card, not credit card: no interest anywhere

These rules come from the code, not from policy.

- **No field can hold interest.** No structure in the module has a rate, an
  APR, a minimum payment, a compounding period or a late-fee field. Statements
  carry only `new_charges` and `amount_due`.
- **Fees are flat and paid by the merchant.** `cn_fee_t` has exactly two
  fields: `kind` and `amount_minor`. `kind` must be `CN_FEE_NONE` (amount 0)
  or `CN_FEE_FLAT_PER_TXN` (at most `CN_FEE_MAX_MINOR`, which is 1,000,000
  minor units). Any other value is rejected by `cn_fee_validate` and
  `cn_cfg_validate`. The test tries 4,096 kinds, and only those two pass. A
  compile-time assertion fails if anyone adds a field to `cn_fee_t`.
- **A fee cannot depend on balance or time.** `cn_fee_for_charge(fee, charge)`
  takes no balance and no date. It returns the flat amount, or 0 if the charge
  is not larger than the fee. The fee is waived in that case so a merchant
  payout is never negative.
- **The default fee is zero.** `cn_cfg_default` sets `CN_FEE_NONE`.
- **The holder pays no fees.** The holder never pays a fee of any kind. The
  merchant fee is deducted from the merchant's payout and posted to the
  network-fee account.
- **Paying late changes nothing about the amount.** If a statement is unpaid
  after its due day, the account becomes `DELINQUENT` and new authorizations
  are declined with code `05`. The amount owed stays the same. The test leaves
  an account unpaid for 1,000 days and checks that the amount owed, the ledger
  and the receipt count are all unchanged to the minor unit. Once the arrears
  are paid in full, the account returns to `ACTIVE`.

Whether a particular charge-card program is lawful and interest-free in a
particular jurisdiction is a legal question. The code only guarantees that its
own arithmetic contains no interest term.

## 4. Configuration (`cn_issuer_cfg_t`)

Start from `cn_cfg_default(&cfg, network)`, change what you need, then call
`cn_issuer_init`. That function runs `cn_cfg_validate` and refuses a bad
configuration.

| Field | Range | Default | Meaning |
|---|---|---|---|
| `network` | `CN_NET_DRAGON`, `CN_NET_PHOENIX`, `CN_NET_THUNDERBIRD` | as passed | which network this issuer runs |
| `issuer_code` | exactly 3 digits | `"001"` | PAN digits 4 to 6 |
| `issuer_seed[32]` | any | all zero | **secret.** It is mixed into every PAN derivation. Set it from a real random source and keep it private. |
| `validity_months` | 1 to 120 | 36 | a card is valid through the last day of the month that falls this many months after issue |
| `cycle_days` | 7 to 62 | 30 | length of the statement cycle |
| `grace_days` | 1 to 62 | 21 | days from statement close to the due day. Pay in full by then. |
| `per_txn_limit[9]` | 0 up to the cycle limit | 1,000,000 for priceable forms, 0 otherwise | largest single charge for each capital form |
| `per_cycle_limit[9]` | 0 up to the ceiling | 5,000,000 for priceable forms, 0 otherwise | total approved per form in one cycle |
| `account_ceiling` | 1 to 999,999,999,999 | 10,000,000 | most an account can owe: held + unbilled + billed |
| `merchant_fee` | see section 3 | `CN_FEE_NONE` | flat fee per settled charge, paid by the merchant |

All amounts are integers in minor units of rail 555.

### The nine forms of capital

Form numbers match `capital_form_t` in `kernel/src/finance/capital_forms.h`. The
test checks this at compile time.

| # | Form | Card spending |
|---|---|---|
| 0 | Social | not allowed (state-reserved, inalienable) |
| 1 | Natural | not allowed (state-reserved, inalienable) |
| 2 | Heritage / Intellectual | not allowed (state-reserved, inalienable) |
| 3 | Governance / Institutional | not allowed (state-reserved, inalienable) |
| 4 | Financial | allowed, with limits |
| 5 | Material | allowed, with limits |
| 6 | Living | allowed, with limits |
| 7 | Knowledge | allowed, with limits |
| 8 | Built | allowed, with limits |

Elsewhere in this repository (`zcapital.h`), forms 0 to 3 are not for sale. To
match that, their limits must be 0, `cn_cfg_validate` rejects any non-zero
limit for them, and `cn_issue_card` refuses to issue a card for them. Each card
carries one form tag and authorizes only that form. A holder who wants to spend
two forms holds two cards on the same account, and the account ceiling covers
both.

### Rails

Every card records rails 555 (debit and asset), 777 (credit and claim) and 888
(equity). These are the constants in `kernel/src/vino_stores/vino_stores.h`,
and the test checks them at compile time. Authorizations must be in currency
555. Anything else is declined with code `57`.

## 5. Operations

| Call | What it does |
|---|---|
| `cn_open_account(iss, now, &acct)` | opens an account. Its first statement cycle starts today. |
| `cn_issue_card(iss, acct, form, holder_pk, entropy, now, &card)` | mints a PAN and binds the holder's ML-DSA-65 public key |
| `cn_reissue_card(iss, card, new_pk_or_NULL, entropy, now, &new_card)` | self-service replacement (see below) |
| `cn_set_frozen(iss, card, true/false)` | freezes or unfreezes. A revoked card cannot be unfrozen. |
| `cn_authorize(iss, req, sig, &auth)` | verifies, checks and records an authorization, approved or declined |
| `cn_settle(iss, auth)` | posts an approved authorization to the ledger and emits a receipt |
| `cn_reverse(iss, auth, now)` | ISO 0400. Releases the hold on an approved authorization, or refunds a settled one. |
| `cn_advance(iss, now)` | closes finished cycles, writes statements and marks past-due accounts. Call it at least daily. |
| `cn_pay(iss, acct, amount, now)` | takes a holder payment. It is applied to billed amounts first, then unbilled. Payments of 0 or more than is owed are refused. |
| `cn_ledger_balanced`, `cn_receipts_verify` | audit checks for C1 and C2 |

**Entropy.** The kernel has no random number generator at this layer. The host
supplies 32 bytes of entropy for each issue or reissue. A new PAN is
`SHAKE256("ZXV-CARDNET-PAN-v1" ‖ network ‖ issuer_seed ‖ entropy ‖ account ‖ generation ‖ attempt)`,
turned into nine unbiased digits. Up to 64 attempts are made to find a
candidate that passes the exclusivity rule and is not already in the table. The
result is deterministic: the same inputs always give the same PAN, so a
derivation can be audited.

**Replacement is all or nothing.** `cn_reissue_card` builds the new card in
full first. Only after every check has passed does it write the new card and
mark the old one `REVOKED`. If minting fails or no slot is free, nothing
changes. The test checks this byte for byte. The new card keeps the account and
the capital form. It can keep the old holder key or take a new one. Its ATC
starts again at 0. Slots that hold revoked cards are reused for later
replacements. After its slot is reused, the old number is simply unknown, and
it is still declined.

**Authorization.** The holder's device builds the challenge with
`cn_challenge(req)`:

`"ZXV-CARDNET-ARQC-v1" ‖ network ‖ PAN ‖ ATC ‖ amount ‖ currency ‖ form ‖ terminal UN ‖ time ‖ STAN ‖ terminal id ‖ merchant id`

The device signs it with ML-DSA-65 under context `ZXV-CARDNET-ARQC-v1`.
`cn_holder_sign` does this when the host holds the key. The issuer checks, in
order:

1. format
2. PAN valid for this network
3. card known
4. not revoked
5. not frozen
6. not expired
7. **signature**
8. ATC strictly greater than the last accepted one
9. currency 555
10. form matches the card
11. account active
12. per-transaction limit
13. per-cycle limit for the form
14. account ceiling

A valid signature uses up its ATC even if a later check declines the
transaction. A bad signature does not.

### Decline codes (ISO 8583 field 39)

| Code | Reasons |
|---|---|
| `00` | approved |
| `05` | signature failed, or account delinquent |
| `14` | not a valid PAN for this network, unknown card, or replaced/revoked card |
| `30` | malformed request |
| `51` | per-cycle limit for the form, or account ceiling |
| `54` | card expired |
| `57` | form not allowed on this card, or currency not 555 |
| `61` | per-transaction limit |
| `62` | card frozen |
| `94` | ATC replay |
| `96` | system error (table full) |

### Ledger postings (VSS C1) and receipts (C2)

| Event | Debit | Credit |
|---|---|---|
| Charge settled | 555 holder receivable: amount | 777 merchant payable: amount − fee; 777 network fees: fee (if any) |
| Refund | 777 merchant payable: amount − fee; 777 network fees: fee | 555 holder receivable: amount |
| Holder payment | 555 issuer cash: amount | 777 holder obligation discharged: amount |

`cn_txn_balanced` checks two things for every transaction, both to the minor
unit with no rounding slack: total debits equal total credits, and the rail-555
total equals the rail-777 total. Every posted transaction emits exactly one
receipt. A receipt names the transaction, the holder account, the
authorization, the merchant, the amount, the fee, the form, the holder's 8-byte
cryptogram and equity rail 888. Each receipt carries the SHA3-256 digest of the
one before it, so `cn_receipts_verify` detects any edit, gap or reordering.

### Statements

When `today ≥ cycle_start + cycle_days`, `cn_advance` closes the cycle:

- unbilled charges move to `billed`,
- a statement is written with `due_day = close + grace_days`,
- per-form cycle spending resets to 0.

A cycle with no new charges writes no statement. If an older amount is still
unpaid, its earlier due day still applies. An account is `DELINQUENT` while
`billed > 0` and today is past the due day.

## 6. ISO 8583:1987 interop

Wire layout: four ASCII MTI digits, then an 8-byte binary primary bitmap, then
an 8-byte secondary bitmap if bit 1 is set, then the fields in ASCII.
Variable-length fields carry a decimal length prefix (LL or LLL).

| Field | Format | Use |
|---|---|---|
| 2 | n..19 LLVAR | PAN |
| 3 | n6 | processing code (`000000` purchase) |
| 4 | n12 | amount in minor units |
| 7 | n10 | transmission date and time, MMDDhhmmss UTC |
| 11 | n6 | STAN |
| 12 / 13 | n6 / n4 | local time hhmmss / local date MMDD |
| 37 | an12 | retrieval reference number |
| 38 | an6 | approval code |
| 39 | an2 | response code (section 5) |
| 41 | ans8 | terminal id |
| 42 | ans15 | card acceptor (merchant) id |
| 49 | n3 | currency, `555` |
| 60 | ans..999 | `VSS1;NET=<D,P,T>;FORM=<0-8>;DR=555;CR=777;EQ=888` |
| 61 | ans..999 | `RCPT=<receipt id>;ATC=<atc>` |
| 63 | ans..999 | `CRY=<64 hex>`: the SHA3-256 of the holder signature |
| 90 | n42 | original data elements, for 0400/0410. Its presence turns on the secondary bitmap. |

Mandatory fields: 0100 and 0200 need 2 3 4 7 11 41 42 49. 0110 and 0210 need
3 4 11 39 41. 0400 needs 2 3 4 11 90. 0410 needs 3 4 11 39 90.

The decoder rejects any unsupported field in the bitmap, any length or
character that does not fit a field's format, truncated input and trailing
bytes. ISO 8583 fields 7, 12 and 13 do not carry a year, so
`cn8583_to_auth_req` takes the year as a parameter.

**The signature does not fit in ISO 8583.** An ML-DSA-65 signature is 3,309
bytes, and no 1987 field holds more than 999. Field 63 carries only the
signature's digest. The issuer needs the signature itself, which comes in the
EMV container or through a side channel. The terminal's unpredictable number
also comes from the EMV data.

## 7. EMV compatibility

`cn_emv_encode_auth` wraps an authorization in the private constructed template
`E1`. Inside it uses standard EMV tags:

- `4F` AID
- `5A` PAN
- `5F24` expiry
- `5F2A` currency (0555)
- `82` AIP
- `95` TVR
- `9A` date
- `9C` type
- `9F02` amount
- `9F16` merchant
- `9F1C` terminal
- `9F21` time
- `9F26` cryptogram
- `9F36` ATC
- `9F37` unpredictable number
- `9F41` sequence counter (carries the STAN)

It also uses three private tags: `DF8101` (the full ML-DSA-65 signature),
`DF8102` (capital form) and `DF8103` (network). The container is about
3,450 bytes.

- The AID is `F0 5A 58 56 43 4E <network>`. ISO/IEC 7816-5 category `F` means
  proprietary and not registered. No RID has been registered.
- `9F26` holds the first 8 bytes of SHA3-256 of the signature. The decoder
  refuses a container in which that binding, the AID's network, or any
  fixed-length tag is wrong.
- AIP and TVR are all zeros. The code claims no EMV features.
- The BER-TLV parser accepts tags of 1 to 3 bytes and definite lengths in short
  form, `81 xx` or `82 xx xx`. It requires minimal length encoding. It rejects
  indefinite length, longer length forms and nesting deeper than 4 levels.

**Out of scope:** EMV certification (EMVCo Level 1, 2 or 3), scheme CA public
keys, issuer master keys, card personalisation, and the EMV card and terminal
protocol itself: SELECT, GPO, READ RECORD, cardholder verification and offline
data authentication. `9F26` here is **not** an EMV ARQC that a scheme host
could verify.

## 8. Mobile money and feature phones

`cn_mobile.h` is the data model and set of state machines that a host-side
adapter drives. It makes no network calls.

- **Wallet.** A phone number (E.164 digits, no `+`, 8 to 15 digits, not
  starting with 0) linked to one card PAN. It stores a salted SHA3-256 PIN
  verifier for a 4 to 6 digit PIN. Three wrong PINs lock the wallet until an
  operator calls `cn_mm_unlock`.
- **Push payment.** The merchant calls `cn_mm_push_create`, and the request is
  `PENDING`. The host sends it to the phone. When the phone side finishes, the
  host calls `cn_mm_push_callback` with result 0 (accepted) or any other value
  (rejected). Callbacks are idempotent: repeating the same outcome is fine, and
  a conflicting outcome is refused. Pending requests expire after
  `push_timeout` seconds. `cn_mm_push_to_auth` turns an accepted request into
  a card authorization request exactly once.
- **USSD menu.** Screens start with `CON` (continue) or `END` and are at most
  182 characters. A session times out after 180 seconds without input. The
  menu offers:
  - 1 Pay merchant: till number (4 to 10 digits, zero-padded to the 15-character
    merchant id), then amount (`12.50`), then PIN, then confirm
  - 2 Statement balance
  - 3 Freeze card
  - 4 Replace card

  Every option except balance ends with an action record that the host carries
  out:
  - `PAY`: call `cn_mm_action_to_auth`, sign, then `cn_authorize`
  - `FREEZE`: call `cn_set_frozen`
  - `REPLACE`: call `cn_reissue_card`, then `cn_mm_relink`

  The confirm screen and the balance screen both state "No interest".

**Custodial signing.** A feature phone cannot hold an ML-DSA key. For USSD and
push flows, the holder's signature must come from a signer that the operator
runs on the holder's behalf, after PIN or operator confirmation. That is weaker
than a key the holder holds, and the operator must tell holders so. A 4 to 6
digit PIN has at most 10⁶ values, so the salted hash protects it at rest only
against casual reading. It does not stop an offline search.

## 9. Building and testing

The host test, from `kernel/`:

```
gcc -std=c11 -Wall -Werror -Wextra -O2 -DTEST_HOST -Iinclude -Isrc/cardnet -Isrc/pqsec -Isrc/mlkem \
  -Isrc/lpres -Isrc/surplus -Isrc/edp_risk -Isrc/event_space -Isrc/modbind -Isrc/trispace \
  -Isrc/finance -Isrc/zcapital -Isrc/vino_stores -Isrc/vino \
  src/cardnet/test_cardnet.c src/cardnet/cn_check.c src/cardnet/cardnet.c src/cardnet/cn_iso8583.c \
  src/cardnet/cn_emv.c src/cardnet/cn_mobile.c src/mlkem/keccak.c $(PQSIG_SRCS) -o /tmp/test_cardnet
/tmp/test_cardnet
```

The test uses real ML-DSA-65 keys from the vendored reference implementation,
which has been checked against NIST ACVP vectors in `test_pq_kat.c`. It runs in
well under a second. It also passes under AddressSanitizer and
UndefinedBehaviorSanitizer.

The module files also cross-compile with
`clang --target=aarch64-none-elf -ffreestanding` and with `gcc -m32
-ffreestanding`. In both builds the only undefined symbols are this module's
own functions, `pq_mldsa65_sign`/`verify`, `sha3_256` and `shake256`. There is
no libc symbol and no libgcc division helper.

## 10. Honest limits

- **No membership or affiliation.** Dragon, Phoenix and Thunderbird are names
  used inside one deployment. They are not members of, affiliated with or
  certified by Visa, Mastercard, EMVCo, any other card scheme, or any
  mobile-money operator.
- **Numbers are not registered.** The 880, 882 and 884 prefixes lie in the
  ISO/IEC 7812 major-industry identifier 8 range and have not been registered
  with the ISO/IEC 7812 registration authority. PANs are valid only inside one
  deployment until that is coordinated. No EMV RID has been registered.
- **No licences, no proven compliance.** The code holds no licence and proves
  no compliance with PCI DSS, PSD2/SCA, consumer-credit or usury law, AML/KYC,
  e-money or payment-service rules. Running a real card or mobile-money program
  requires those licences and legal advice.
- **Check digits are not authority.** A check digit shows only that a number is
  self-consistent. C5 holds because of prefix plus exclusivity (section 2), not
  because of the check digit alone.
- **Issuer and key custody are the host's job.**
  - Receipts are hash-chained but not signed by the issuer.
  - PAN uniqueness is checked only against the cards in this issuer's table.
  - Holder private keys never enter the module.
  - Secure elements, HSMs and custodial signers belong to the host.
- **Interop is partial.**
  - The ISO 8583 codec implements a general 1987 subset, not any network's
    dialect, and computes no MAC.
  - The EMV container is EMV-shaped data, not an EMV kernel.
  - The full ML-DSA signature does not fit in ISO 8583.
- **Model scope.**
  - The issuer is single-threaded. `cn_issue_card`, `cn_reissue_card` and
    `cn_emv_encode_auth` each use one static scratch buffer.
  - Tables are fixed: 8 accounts, 16 cards, 32 authorizations, 64
    transactions and receipts, and 32 statements per issuer. A full table
    returns `CN_ERR_FULL` and changes nothing.
  - A refund after the charge has already been paid is outside this model, and
    is refused with `CN_ERR_STATE`.

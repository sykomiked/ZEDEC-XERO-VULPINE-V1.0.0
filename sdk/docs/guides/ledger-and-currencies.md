<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Ledger and currencies

**Who this is for:** developers building wallets, treasuries, settlement
back offices or anything that moves value between accounts.
**What you will be able to do:** enable currencies, open accounts, post
transfers, read balances and journals, check the books, and write an ISO
20022 payment message.

All examples are Python, run against the SDK. Other SDKs use the same names
(`Ledger`, `openAccount`, `post`, `balance`).

## Currencies

A context starts with no currencies. Enable the ones you use (needs
`LEDGER_POST`):

```python
import zxv

with zxv.Context() as ctx:
    root = ctx.root_capability()
    ctx.currencies.enable("NGN", cap=root)
    ctx.currencies.enable("GHS", cap=root)
    ctx.currencies.get("NGN")
    # EnabledCurrency(code='NGN', minor_units=2, flags=<CurrencyFlag.PAYABLE: 8>)
```

The codes come from **ISO 4217**, the international list of currency codes.
The library has the table built in: `zxv.iso4217.lookup("NGN")` gives the
numeric code 566, 2 minor units and the name "Naira".

### Private units

You can also register a unit of your own, such as loyalty points or an
internal token:

```python
ctx.currencies.register_private("VFV", 2, cap=root)   # 2 decimal places
```

A private unit works in the ledger like any currency, but it is **ledger
only**: it is not an ISO 4217 code, so it is refused in a payment message
(see below).

## Amounts are integers

Every amount is a whole number of the currency's **minor unit**: kobo for
NGN, pesewas for GHS. 150.00 NGN is `15000`. The largest amount is
`zxv.AMOUNT_MAX` (2^53 − 1). `0`, a negative number, or anything larger is
refused with `ZXV_E_RANGE`, and a float with `ZXV_E_FLOAT`.

## Accounts: issuers and holders

- An **issuer** account is where value enters the ledger: a central bank, a
  treasury, a mint. Its balance may go below zero.
- A **holder** account belongs to a customer or a wallet. It can never go
  below zero. A transfer that would make it negative is refused with
  `ZXV_E_FUNDS`.

Each account has one currency. A transfer between accounts of different
currencies is refused with `ZXV_E_CURRENCY`. ZXV does not do currency
exchange.

```python
L = zxv.Ledger(root)
cbn = L.open_account("issuer", "NGN", zxv.AccountKind.ISSUER)
ada = L.open_account("ada", "NGN")              # a holder by default
bola = L.open_account("bola", "NGN")
L.post(cbn, ada, 10_000, "NGN", "MINT-1")       # 100.00 NGN into the ledger
L.post(ada, bola, 2_550, "NGN", "E2E-1")        # ada pays bola 25.50 NGN
```

Every posting has a **reference**: up to 35 bytes, unique in the ledger
(posting the same reference twice gives `ZXV_E_DUPLICATE`, a longer one
`ZXV_E_TOO_LONG`). Thirty-five is
the length of an ISO 20022 end-to-end ID, so you can use the same value.

## Three balances per account

Each account keeps three numbers, called **rails**:

| Rail | Code | Meaning |
|---|---|---|
| debit | 555 | everything the account has received |
| credit | 777 | everything the account has sent |
| equity | 888 | debit − credit: what it holds now |

```python
for a in (cbn, ada, bola):
    print(L.account_name(a), L.balance(a))
# issuer Balance(debit=0, credit=10000, equity=-10000)
# ada Balance(debit=10000, credit=2550, equity=7450)
# bola Balance(debit=2550, credit=0, equity=2550)
```

This is the accounting identity Assets = Liabilities + Equity, in its
simplest form: what came in, what went out, and the difference. The issuer's
negative equity is the value it has put into circulation.

## Journal and checks

The journal lists every posting that touched an account. Amounts are signed
from that account's side:

```python
L.journal(ada)
# [JournalEntry(entry_id=1, amount=10000, counterparty=0, reference='MINT-1'),
#  JournalEntry(entry_id=2, amount=-2550, counterparty=2, reference='E2E-1')]
```

`L.check()` checks the whole ledger: for each currency, total debits equal
total credits, and no holder is below zero. Run it after a batch, and in
your tests. If it fails, the context is halted and you get the fatal error
(see [Errors and fail-closed](errors-and-fail-closed.md)).

## ISO 20022 messages

**ISO 20022** is the XML message standard many payment systems use. ZXV can
write a `pacs.008` (a customer credit transfer) and a `camt.053` (a
statement). It only writes the text. It has **no network connection** and
sends nothing.

```python
from zxv import Cap, MxMessage, MxKind, MxField, MxAmount

with MxMessage(root.attenuate(Cap.LEDGER_READ), MxKind.PACS008) as m:
    m.set_text(MxField.MSG_ID, "M1").set_text(MxField.CRE_DT_TM, "2026-10-11T09:00:00Z")
    m.set_text(MxField.DEBTOR_NAME, "Ada").set_text(MxField.DEBTOR_ACCT, "NG01")
    m.set_text(MxField.CREDITOR_NAME, "Bola").set_text(MxField.CREDITOR_ACCT, "NG02")
    m.set_text(MxField.END_TO_END_ID, "E2E-1").set_text(MxField.CCY, "NGN")
    m.set_amount(MxAmount.SETTLEMENT, 15000, 2)       # 150.00, exactly
    xml = m.render()        # ... <IntrBkSttlmAmt Ccy="NGN">150.00</IntrBkSttlmAmt> ...
```

Setting `CCY` to a private unit such as `VFV` is refused with
`ZXV_E_CURRENCY`: "'VFV' is not ISO 4217; private units (e.g. VFV) and rail
numerics 555/777/888 never go in a Ccy attribute".

## What this ledger is and is not

- It is an **in-process ledger**: it lives in your program's memory. Saving
  it, replicating it and backing it up are up to you.
- Its message formats are **designed to support** integration with payment
  networks such as PAPSS and SWIFT. ZXV is not connected to them, has no
  partnership with them, and makes no claim of compliance or certification
  by any payment scheme. Connecting to a real network needs that network's
  own onboarding and testing.
- Netting cycles, card issuing and some message kinds are part of the ABI
  but return `ZXV_E_NOTIMPL` in the current build. Ask the library what is
  live with `zxv.has_feature(zxv.Feature.MX_PACS008)` (or `zxv.features()`).

See also [the security model](../SECURITY.md).

## Next

- [Capabilities](capabilities.md): separate the teller from the auditor.
- [Spring and ASP.NET Core](frameworks.md): a ledger behind a web API.

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Check a payment message, field by field

Every field of one payment message (reference, amount, currency, the two
accounts, the value date) is checked against exact types written in Eve:

- the amount is a decimal with exactly 2 places: `1234.567` is refused,
  **never rounded**;
- the currency must be one of the listed ISO 4217 codes: `xyz` is refused;
- accounts and dates must sit inside their declared ranges.

The same record is also written as an Ada package specification
(`payment.ads`), the way many core banking and RTGS systems declare it.
The run checks that both files declare exactly the same types (their
SHA3-256 digests match). If someone changes one and not the other, the
run is refused and shows the first difference.

## Run it

```sh
zxv new payment-message-check
cd payment-message-check
zxv run
```

As shipped, the result is TRUE, with every field read back exactly and
the layout's digest.

- To see a **refusal**, set `amount = 1234.567` in `inputs.txt`, or
  `ccy = xyz`.
- To see **drift** refused, change `2**32` to `2**31` in `payment.ads`.
- To see **pending**, delete a line from `inputs.txt`: the step names
  the missing field.

You can ask the local AI to draft `payment.eve` for a record you
describe. Its draft is checked exactly like anything you type.

## Files

- `payment.eve`: the record, in Eve.
- `payment.ads`: the same record, in Ada.
- `inputs.txt`: one payment message, one line per field.

## Limits

It checks one message's fields and layout; it does not parse ISO 20022
XML or SWIFT MT, does not check that a date exists on the calendar or
that an account is open, and does not send anything. It is designed to
support a payments team's review, not replace their controls.

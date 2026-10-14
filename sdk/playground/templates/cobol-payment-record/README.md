# Read a COBOL payment record exactly

Most of the world's payments still pass through COBOL. The record format
is a *copybook*, and every system that reads the bytes must agree on it to
the last digit. This template shows ZXV reading such a record exactly.

```text
copybook   05 PAY-AMOUNT  PIC S9(13)V99 COMP-3.
Sutra        pay-amount: s9(13)v99 comp-3
```

One step, four checks:

1. **Same layout.** `payment.cpy` (the copybook) and `payment.sutra`
   (the same record in Sutra) must give the same digest, the SHA3-256 of
   the canonical layout. If someone edits one and not the other, the run
   is refused at the line that differs.
2. **Exact values.** Each line of `inputs.txt` fills one field. Money is
   packed decimal (COMP-3), never a float. A value that does not fit its
   PICTURE is refused, naming the field: never truncated, never rounded.
   Every field is read back, and `hex` shows the record's 55 bytes.
3. **Their copy.** `counterparty.txt` is the same payment as the other
   bank's system reports it. Fields that agree pass. A field that
   differs is **held** for a person (BOTH), not resolved for you. A field
   only one side reports is **pending** (NEITHER).
4. **The posting.** The amount, currency, accounts and reference are read
   as one ledger posting (`1001->2002 1234.56 USD ...`). It is only read:
   the Playground never posts to anyone's books.

## Run it

```sh
zxv new cobol-payment-record
cd cobol-payment-record
zxv run
```

As shipped, everything agrees: TRUE.

- **See a refusal:** set `pay-amount = 12345678901234.56` in `inputs.txt`
  (14 digits before the point; the field holds 13). Or change
  `S9(13)V99` to `S9(11)V99` in `payment.cpy` only.
- **See a hold:** change `pay-amount` in `counterparty.txt` to `1243.56`.
- **See pending:** delete the `pay-fee` line from `counterparty.txt`.

## Draft it with the local AI

**Draft with AI** writes `payment.sutra` from a plain description of your
record. The draft is untrusted text: it must parse and must match
`payment.cpy` by digest before anything else runs.

## Limits

ASCII records only (no EBCDIC), no REDEFINES, no OCCURS DEPENDING ON, no
CICS or IMS. This shows how a record is read; it is not a mainframe
certification and it does not connect to a payment system.

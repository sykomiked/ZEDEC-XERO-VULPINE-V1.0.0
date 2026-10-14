# Reconcile payments before settling

Three sources report on each payment: the sender's bank, the receiver's
bank and the RTGS (the settlement system). Before the day's net settlement
runs, every payment must be confirmed.

ZXV does not force the reports into yes or no. It keeps four answers:

| Reports | Verdict | What happens |
|---|---|---|
| only confirmations | TRUE | clear |
| only denials | FALSE | clear: the payment did not settle |
| confirmations and denials | BOTH | **held** for a person to resolve |
| no report yet | NEITHER | pending |

A contradiction is held and shown, never averaged away and never allowed
to "explode" into an arbitrary answer. The settlement step is `gate`d: it
runs only when every payment is clear.

## Run it

```sh
zxv new payment-reconciliation
cd payment-reconciliation
zxv run
```

As shipped, P-1002 is held (the receiver denies it), so settlement does
not run. Change the receiver's `-1` to `1` in `inputs.txt` and run again:
everything is TRUE and the end-of-day treaty settles on a scratch engine.

## Files

- `inputs.txt`: the reports, one column per payment.
- `reconcile.k`: KRON (K spelling) that counts confirmations and denials.
- `end-of-day.treaty`: the net settlement, in treaty Sutra.

## Limits

A demonstration on a scratch engine. It does not connect to a real RTGS,
and its verdicts support a person's review; they do not replace it.

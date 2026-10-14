# Check an account statement adds up

A monthly statement from a COBOL core: an account, an opening and a
closing balance (packed decimal) and a table of up to five lines
(`OCCURS 5`). Two steps:

1. **statement** (Sutra) builds the record from `inputs.txt`, proves
   `statement.cpy` and `statement.sutra` declare the same bytes, and reads
   every field back exactly, line by line. Fields with no input stay as
   COBOL initializes them (zeros, spaces), and a note lists them. It
   hands the opening, the closing and the five line amounts to step 2.
2. **balance** (KRON) checks, to the cent, that
   `opening + sum(lines) = closing`. It is `gate`d: it runs only when the
   record was read cleanly.

```text
10000.00 + 1500.00 - 600.00 + 12.50 - 27.75 = 10884.75   TRUE
```

## Why a mismatch is BOTH, not FALSE

The statement makes two claims about the same balance: the closing figure
it states, and the figure its own lines add up to. When they differ, the
data contradicts itself. ZXV cannot know which claim is wrong: a missing
line, a mistyped amount, or a wrong closing balance. Calling it FALSE
would quietly pick a side. So it is held (BOTH) for a person to resolve,
with nothing averaged or rounded away.

## Run it

```sh
zxv new account-statement
cd account-statement
zxv run
```

- **See a hold:** change `st-closing` in `inputs.txt` to `10912.50`.
- **See a refusal:** make a line's text longer than 20 characters, for
  example `st-line-text(2) = Rent for September, flat 4B`. Text is never
  cut short; the run is refused at the field's line in `statement.sutra`.

## Files

- `statement.sutra`, `statement.cpy`: the same layout, twice.
- `inputs.txt`: the statement's values; `st-line-amount(2)` is line 2.
- `balance.k`: the check, in KRON (K spelling).

## Limits

One statement and five lines; ASCII records only. The check supports a
person's review of a statement; it is not an audit opinion.

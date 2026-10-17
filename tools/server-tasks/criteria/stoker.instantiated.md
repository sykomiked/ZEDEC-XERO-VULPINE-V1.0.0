# Criteria (instantiated) — stoker

matrix 196 · wave spec `wave16/196-stoker.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| model admission log (model, signature result, licence code)  | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- "admit this model" (signed AND licence allows commercial use) in Ada as Eve + APL as KRON witnessed, refusing unsigned or non-commercial weights
- tokens per second per watt (tokens x 10^6 / microjoules) in FORTRAN as Trebago + APL as KRON witnessed
- layers placed in video memory (largest prefix of layer bytes that fits the free bytes) in APL as KRON + FORTRAN as Trebago witnessed
- model catalogue in MUMPS as MYRA

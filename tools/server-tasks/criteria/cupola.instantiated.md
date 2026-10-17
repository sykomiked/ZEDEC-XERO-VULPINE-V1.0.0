# Criteria (instantiated) — cupola

matrix 282 · wave spec `wave23/282-cupola.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `retention_ledger` (copies held per record class, refused on | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `copies_ok` (at least the declared copies, on at least two kinds of media, each verified within its interval) in Ada as Eve + APL as KRON witnessed
- `decay_risk` (months since the last verify x the media's declared risk) in FORTRAN as Trebago + APL as KRON witnessed
- `integrity` grid (copy by check) in APL as KRON
- `archive` hierarchy in MUMPS as MYRA

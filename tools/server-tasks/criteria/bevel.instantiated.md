# Criteria (instantiated) — bevel

matrix 318 · wave spec `wave26/318-bevel.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `records` hierarchy in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `redact` (a field is redacted unless every rule marks it public) in Ada as Eve + APL as KRON witnessed
- `reid_risk` (smallest group size sharing the kept fields, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `disclosure_log` (disclosure number, recipient, fields, never reused) in COBOL as Sutra
- `sensitivity` grid (field by rule) in APL as KRON

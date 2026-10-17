# Criteria (instantiated) — trowel

matrix 184 · wave spec `wave15/184-trowel.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| remaining life (expected life minus age, months) exact in FO | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- "safety check overdue" (days since the last gas, electrical or alarm inspection over the declared interval) in Ada as Eve + APL as KRON witnessed
- repair invoices per item and per year in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- room grid in APL as KRON
- property tree in MUMPS as MYRA

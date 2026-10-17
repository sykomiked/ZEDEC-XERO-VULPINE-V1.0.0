# Criteria (instantiated) — skillet

matrix 159 · wave spec `wave13/159-skillet.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| shopping list (needed minus in the pantry, never below zero, | `difference` | num | `a,b` | trebago, kron | a - b exactly (never negative in this app's domain) | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## Clauses the pattern library does not yet cover (author these)

- "keep off the plate" (any household member excludes one of the ingredient's allergens, over the household) in Ada as Eve + APL as KRON witnessed
- grocery spend per week in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- portions for the household (Kettle's scaling) in FORTRAN as Trebago
- pantry grid in APL as KRON
- household tree in MUMPS as MYRA

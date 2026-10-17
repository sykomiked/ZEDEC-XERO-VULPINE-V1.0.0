# Criteria (instantiated) — compositor

matrix 165 · wave spec `wave13/165-compositor.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| job records (job number, fuel spent, refused on overflow) in | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| numeric routines (run time totals) exact in FORTRAN as Treba | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- "outputs agree" for one script run in two languages (output vectors equal, element by element) in Ada as Eve + APL as KRON witnessed, joined with Colonnade's verdict
- script contracts (declared input ranges) in Ada as Eve
- the array REPL is a KRON session
- global trees are a MYRA browser

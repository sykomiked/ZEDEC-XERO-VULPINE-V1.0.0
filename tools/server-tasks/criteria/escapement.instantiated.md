# Criteria (instantiated) — escapement

matrix 172 · wave spec `wave14/172-escapement.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| timesheets (minutes per shift per person, weekly sums refuse | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## Clauses the pattern library does not yet cover (author these)

- clock offset (median of the peers' offsets in ms) in FORTRAN as Trebago + APL as KRON witnessed
- "job may run" (inside its window, deadline not passed) in Ada as Eve + APL as KRON witnessed
- schedule matrix (person by shift, nobody booked twice at once) in APL as KRON
- shift tree in MUMPS as MYRA

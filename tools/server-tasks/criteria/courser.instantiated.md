# Criteria (instantiated) — courser

matrix 188 · wave spec `wave15/188-courser.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "over the heart-rate limit" (above the declared maximum) in  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "over the heart-rate limit" (above the declared maximum) in  | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## Clauses the pattern library does not yet cover (author these)

- pace (seconds per km from time and distance, floor) in FORTRAN as Trebago + APL as KRON witnessed
- interval grid in APL as KRON
- athlete tree in MUMPS as MYRA

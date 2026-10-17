# Criteria (instantiated) — swage

matrix 146 · wave spec `wave12/146-swage.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "may ship" = Whetstone's verdict TRUE AND Satchel's limits h | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| build timing (stage milliseconds summed) exact in FORTRAN as | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| declared resource limits (fuel, memory, files) in Ada as Eve | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- release ledger (major.minor.patch strictly above the last release, release number refused on overflow) in COBOL as Sutra + APL as KRON witnessed
- dependency order (every dependency built before its dependant, over the dependency matrix) in APL as KRON
- package tree in MUMPS as MYRA

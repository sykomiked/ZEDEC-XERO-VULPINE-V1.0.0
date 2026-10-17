# Criteria (instantiated) — tephra

matrix 328 · wave spec `wave27/328-tephra.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `evacuate_advice` (readiness level from the declared thresho | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `response_budget` (in minor units) in COBOL as Sutra | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## Clauses the pattern library does not yet cover (author these)

- an evacuation advice needs two instruments over threshold) in Ada as Eve + APL as KRON witnessed
- `seismic` (event magnitude from integer amplitude tables) in FORTRAN as Trebago + APL as KRON witnessed
- `sensors` arrays in APL as KRON
- `zones` hierarchy in MUMPS as MYRA

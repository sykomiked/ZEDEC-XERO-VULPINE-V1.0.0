# Criteria (instantiated) — windlass

matrix 340 · wave spec `wave28/340-windlass.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `gas_depth_ok` (gas readings and working depth inside the de | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ore_grade` (grade per block from assays) in FORTRAN as Treb | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## Clauses the pattern library does not yet cover (author these)

- `royalty_ledger` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `assays` grid in APL as KRON
- `crew_health` tree in MUMPS as MYRA

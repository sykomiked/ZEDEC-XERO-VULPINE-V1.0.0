# Criteria (instantiated) — lampwright

matrix 205 · wave spec `wave17/205-lampwright.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "within brightness limits" (at most the room's cap, the nigh | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "within brightness limits" (at most the room's cap, the nigh | `within_range` | truth | `value,min,max` | eve, kron | true when min <= value <= max | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |

## Clauses the pattern library does not yet cover (author these)

- colour temperature to RGB (table by kelvin, interpolated) in APL as KRON + FORTRAN as Trebago witnessed
- energy use (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- light grid in APL as KRON
- room tree in MUMPS as MYRA

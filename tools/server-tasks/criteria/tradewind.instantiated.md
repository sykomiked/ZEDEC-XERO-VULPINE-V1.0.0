# Criteria (instantiated) — tradewind

matrix 211 · wave spec `wave17/211-tradewind.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| best order of stops (shortest total over the distance matrix | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| "within load and hours limits" (load at most the vehicle's,  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "within load and hours limits" (load at most the vehicle's,  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- stop filters as K queries in APL as KRON
- waybills (waybill number, charged weight, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- stop hierarchy in MUMPS as MYRA

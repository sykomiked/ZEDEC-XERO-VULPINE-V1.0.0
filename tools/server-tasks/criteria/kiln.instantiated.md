# Criteria (instantiated) — kiln

matrix 186 · wave spec `wave15/186-kiln.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| "schedule allowed" (peak at most the kiln's maximum AND ramp | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "schedule allowed" (peak at most the kiln's maximum AND ramp | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| job costing (material grams x price plus energy, minor units | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| layer arrays (layer count, area per layer) in APL as KRON | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- firing schedule temperature at each step (ramp in tenths of a degree an hour) in FORTRAN as Trebago + APL as KRON witnessed
- batch tree in MUMPS as MYRA

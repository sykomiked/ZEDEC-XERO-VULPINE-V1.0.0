# Criteria (instantiated) — dulcimer

matrix 342 · wave spec `wave28/342-dulcimer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `furnace_ok` (melt temperature at most the furnace's and the | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `furnace_ok` (melt temperature at most the furnace's and the | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `alloy_mix` (grams of each metal for a fineness, exact) in FORTRAN as Trebago + APL as KRON witnessed
- `orders` (workshop orders in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `stock` grid in APL as KRON
- `pieces` lineage tree in MUMPS as MYRA

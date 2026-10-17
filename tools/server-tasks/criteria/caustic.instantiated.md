# Criteria (instantiated) — caustic

matrix 329 · wave spec `wave27/329-caustic.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `dive_ok` (depth and time inside the declared no-stop limits | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grant_accounts` (in minor units) in COBOL as Sutra + FORTRA | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `heat_stress` (degree heating weeks from daily temperatures) in FORTRAN as Trebago + APL as KRON witnessed
- `plots` grid in APL as KRON
- `species` tree in MUMPS as MYRA

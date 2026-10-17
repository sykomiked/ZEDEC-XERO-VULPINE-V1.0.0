# Criteria (instantiated) — meridian

matrix 291 · wave spec `wave24/291-meridian.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `window_go` (wind, wave and visibility inside the declared l | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `voyage_accounts` (fuel and stores in minor units) in COBOL  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `crew_vessel` records in MUMPS as MYRA | `composite_key` | num | `high,low` | myra, kron | the record's index: high * 1000 + low (the cellar's composite key) | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## Clauses the pattern library does not yet cover (author these)

- `tide_current` (current along a leg from tide times, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `routes` grid in APL as KRON

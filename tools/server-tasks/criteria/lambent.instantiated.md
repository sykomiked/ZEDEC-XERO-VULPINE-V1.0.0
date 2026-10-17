# Criteria (instantiated) — lambent

matrix 288 · wave spec `wave24/288-lambent.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `load_ok` (planned hours this week at most the declared week | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `load_ok` (planned hours this week at most the declared week | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `training_costs` (courses in minor units) in COBOL as Sutra  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `progress_forecast` (weeks to a milestone at the recent pace) in FORTRAN as Trebago + APL as KRON witnessed
- `skills` matrix in APL as KRON
- `milestones` tree in MUMPS as MYRA

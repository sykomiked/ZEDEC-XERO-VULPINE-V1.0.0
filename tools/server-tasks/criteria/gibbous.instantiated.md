# Criteria (instantiated) — gibbous

matrix 283 · wave spec `wave23/283-gibbous.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `routine_ok` (a suggested bedtime shift at most 30 minutes a | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `routine_ok` (a suggested bedtime shift at most 30 minutes a | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `plan_cost` (a wellness plan's items in minor units) in COBO | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## Clauses the pattern library does not yet cover (author these)

- `circadian` (sleep midpoint and its drift over 14 nights) in FORTRAN as Trebago + APL as KRON witnessed
- `night` sensor arrays in APL as KRON
- `sleep_history` in MUMPS as MYRA

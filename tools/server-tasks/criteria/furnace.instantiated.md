# Criteria (instantiated) — furnace

matrix 199 · wave spec `wave16/199-furnace.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| energy accounts (Wh x tariff in minor units) in COBOL as Sut | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| "within grid limits" (draw at most the inverter's maximum AN | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "within grid limits" (draw at most the inverter's maximum AN | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |

## Clauses the pattern library does not yet cover (author these)

- expected solar Wh (panel watts x sun minutes / 60) in FORTRAN as Trebago + APL as KRON witnessed
- load grid in APL as KRON
- site tree in MUMPS as MYRA

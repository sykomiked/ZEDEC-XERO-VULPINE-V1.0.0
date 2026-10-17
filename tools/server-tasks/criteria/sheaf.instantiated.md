# Criteria (instantiated) — sheaf

matrix 170 · wave spec `wave14/170-sheaf.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| farm accounts per field and enterprise in minor units in COB | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| "application allowed" (rate at most the label maximum AND th | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| "application allowed" (rate at most the label maximum AND th | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| scale reading (raw load-cell counts to grams with the calibr | `within_range` | truth | `value,min,max` | eve, kron | true when min <= value <= max | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| scale reading (raw load-cell counts to grams with the calibr | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- harvest per hectare per field (kilograms) exact in FORTRAN as Trebago
- field grid in APL as KRON
- herd tree in MUMPS as MYRA

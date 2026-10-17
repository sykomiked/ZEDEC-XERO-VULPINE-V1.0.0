# Criteria (instantiated) — solarium

matrix 292 · wave spec `wave24/292-solarium.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| `evapotranspiration` (daily water loss from temperature and  | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `produce_accounts` (seeds and produce in minor units) in COB | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `produce_accounts` (seeds and produce in minor units) in COB | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## Clauses the pattern library does not yet cover (author these)

- `water_ok` (Dibber's "may water now" AND soil below the declared moisture AND no rain in the forecast window) in Ada as Eve + APL as KRON witnessed
- `beds` sensor grid in APL as KRON
- `lineage` (plant lineage) in MUMPS as MYRA

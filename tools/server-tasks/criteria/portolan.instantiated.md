# Criteria (instantiated) — portolan

matrix 344 · wave spec `wave29/344-portolan.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `no_fly`: no-fly zone limits for drone scans (inside a dec | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `no_fly`: no-fly zone limits for drone scans (inside a dec | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `registry_fee`: parcel registry fees (minor units, refusin | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - `registry_fee`: parcel registry fees (minor units, refusin | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| Paraconsistent (BOTH test, in test_zxapp.c): survey A puts a | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| Paraconsistent (BOTH test, in test_zxapp.c): survey A puts a | `clamp` | num | `value,min,max` | kron, trebago | value clamped into [min,max] | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| the test asserts state 3 and that the map draws both lines a | `modular_draw` | num | `seed,members,step` | trebago, sutra | the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) m | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| the test asserts state 3 and that the map draws both lines a | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `merge_ok`: (critical) "merge this source" (its survey closes within tolerance AND its date is known AND its datum is one of the declared ones) in Ada as Eve + APL as KRON witnessed
- - `project`: (critical) projection from microdegrees to grid millimetres by an integer table (transverse Mercator series in fixed point) in FORTRAN as Trebago + APL as KRON witnessed
- - `elevation`: elevation grids (cells x metres, slope per cell) in APL as KRON
- - `places`: place hierarchy (region, parcel, building, route) in MUMPS as MYRA
- never re-derive the tables

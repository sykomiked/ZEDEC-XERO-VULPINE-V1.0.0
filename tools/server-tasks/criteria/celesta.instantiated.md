# Criteria (instantiated) — celesta

matrix 406 · wave spec `wave34/406-celesta.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `slope_ok`: (critical) slope angle limits (no route over t | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `slope_ok`: (critical) slope angle limits (no route over t | `grade_band` | num | `value,b1,b2,b3,b4` | kron, trebago | the band index 0..4 of the value across the four ascending bounds | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| - `report`: trip report counts (members, check-ins, alerts)  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `passes`: lift pass sales (minor units, refusing on overfl | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| Paraconsistent (BOTH test, in test_zxapp.c): two sources rat | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that routes use 4 while showing | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `snowpack`: (critical) snowpack model (load from new snow and wind, integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `terrain`: terrain grid in APL as KRON
- - `party`: party tree in MUMPS as MYRA
- never re-derive the tables

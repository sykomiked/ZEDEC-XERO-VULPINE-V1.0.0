# Criteria (instantiated) — manometer

matrix 395 · wave spec `wave33/395-manometer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `interlock`: (critical) interlock limits (any redundant ch | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `report`: regulator report counts (events per kind per per | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `billing`: generation billing (MWh x tariff, minor units,  | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that a safe hold is proposed, n | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `thermal`: (critical) thermal hydraulics (heat balance, flow x temperature rise, integers) in FORTRAN as Trebago + APL as KRON witnessed
- - `sensors`: sensor grid in APL as KRON
- - `plant`: plant component tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two redundant pressure channels read 15.2 and 16.9 MPa: the channel pair is BOTH
- never re-derive the tables

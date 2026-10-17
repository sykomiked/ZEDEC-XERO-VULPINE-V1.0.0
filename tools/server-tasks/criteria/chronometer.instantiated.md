# Criteria (instantiated) — chronometer

matrix 404 · wave spec `wave34/404-chronometer.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `rest_ok`: (critical) rest limits (at least the declared h | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `rest_ok`: (critical) rest limits (at least the declared h | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `billable`: billable hours (minutes x rate, minor units, r | `ratio_x1000` | num | `num,den` | kron, trebago | the ratio in thousandths, floor: num * 1000 / den | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| the test asserts state 3 and that it is held with a window c | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `drift`: (critical) clock drift correction (offset trend per hour from paired readings) in FORTRAN as Trebago + APL as KRON witnessed
- - `parallel`: booking conflicts across people and rooms in APL as KRON
- - `rota`: rota grid in APL as KRON
- - `team`: team tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two trusted clocks differ by 900 ms for one event: the timestamp is BOTH
- never re-derive the tables

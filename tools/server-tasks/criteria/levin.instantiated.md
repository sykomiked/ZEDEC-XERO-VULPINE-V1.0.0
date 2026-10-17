# Criteria (instantiated) — levin

matrix 236 · wave spec `wave19/236-levin.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - (critical) "circuit within its breaker" (load at most 80%  | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - (critical) "circuit within its breaker" (load at most 80%  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - permit records (permit fees in minor units and inspection  | `product` | num | `a,b` | sutra, kron | a * b exactly in minor units | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| - permit records (permit fees in minor units and inspection  | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| the test asserts state 3 and a miswiring flag. Join the sour | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - (critical) load and voltage drop (current in mA from W and V
- drop = 2 x length x current x resistance per metre, integer milliohms) in FORTRAN as Trebago + APL as KRON witnessed
- - circuit matrix (circuits x rooms x loads) in APL as KRON
- - panel tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the plan puts the kitchen's 2 kW on circuit 3 while the reading shows circuit 3 idle and circuit 4 carrying it, giving BOTH
- never re-derive the tables

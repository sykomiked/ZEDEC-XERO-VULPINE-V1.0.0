# Criteria (instantiated) — seine

matrix 382 · wave spec `wave32/382-seine.md`

| the spec's clause | pattern | kind | args | languages | logic | exact check |
|---|---|---|---|---|---|---|
| - `battery_ok`: (critical) battery limits (a node sends at m | `limit_excess` | num | `value,limit` | trebago, eve | 0 when value <= limit, else value - limit (the ceiling's excess) | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| - `battery_ok`: (critical) battery limits (a node sends at m | `count_ok` | truth | `count,max` | eve, kron | true when 0 < count <= max (the registry's law) | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| - `filter`: (critical) signal filtering (median of 5 then a  | `exact_total` | num | `n,a,b,c,d` | trebago | the exact sum of the first n of a,b,c,d (the accumulator law) | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| - `credits`: data sharing credits (readings shared, refusing | `share_x100` | num | `part,whole` | kron, trebago | the part's share of the whole in hundredths of a percent, floor: part  | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| the test asserts state 3 and that the map shows the anomaly, | `running_state` | truth | `state,event` | eve, sutra | the transition's legality: state + event -> true when the move is allo | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## Clauses the pattern library does not yet cover (author these)

- - `relay`: relay choice (next hop by link margin) in APL as KRON
- - `readings`: reading arrays in APL as KRON
- - `nodes`: node tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two neighbouring sensors read 7 and 19: the cell is BOTH
- never re-derive the tables
